/* DiskFlashback, Copyright (C) 2021-2024 Robert Smith (@RobSmithDev)
 * https://robsmithdev.co.uk/diskflashback
 *
 * This file is multi-licensed under the terms of the Mozilla Public
 * License Version 2.0 as published by Mozilla Corporation and the
 * GNU General Public License, version 2 or later, as published by the
 * Free Software Foundation.
 *
 * MPL2: https://www.mozilla.org/en-US/MPL/2.0/
 * GPL2: https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 *
 * This file is maintained at https://github.com/RobSmithDev/DiskFlashback
 */


#include "SCPFile.h"


#pragma pack(1) 
/* Taken from https://www.cbmstuff.com/downloads/scp/scp_image_specs.txt
This information is copyright(C) 2012 - 2020 By Jim Drew. Permission is granted
for inclusion with any source code when keeping this copyright notice.
*/
struct SCPFileHeader {
	int8_t	headerSCP[3];
	uint8_t	version;
	uint8_t	diskType;
	uint8_t	numRevolutions;
	uint8_t	startTrack;
	uint8_t endTrack;
	uint8_t	flags;
	uint8_t	bitcellEncoding;   // 0=16 bits per sample, 
	uint8_t	numHeads;
	uint8_t timeBase;          // Resolution. Time in ns = (timeBase+1)*25
	uint32_t checksum;
};

struct SCPTrackHeader {
	int8_t headerTRK[3];
	uint8_t	trackNumber;
};

struct SCPTrackRevolution {
	uint32_t indexTime;		// Time in NS/25 for this revolution
	uint32_t trackLength;	// Number of bit-cells in this revolution
	uint32_t dataOffset;		// From the start of SCPTrackHeader 
};
#pragma pack()

#define BITFLAG_INDEX		0
#define BITFLAG_96TPI		1
#define BITFLAG_NORMALISED  3
#define BITFLAG_EXTENDED    6
#define BITFLAG_FLUXCREATOR 7

// Actually read the file
bool SCPFile::readSCPFile() {
	DWORD read;
	SCPFileHeader header;
	SetFilePointer(m_file, 0, NULL, FILE_BEGIN);
	if (!ReadFile(m_file, &header, sizeof(header), &read, NULL)) read = 0;
	if (read != sizeof(header)) return false;

	m_fluxMultiplier = (header.timeBase + 1) * 25;
	m_firstTrack = header.startTrack;
	m_lastTrack = header.endTrack;
	m_numRevolutions = header.numRevolutions;
	if (m_lastTrack >= 168) return false;
	// only support 16-bit data
	if (!((header.bitcellEncoding == 0) || (header.bitcellEncoding == 16))) return false;

	// Read the offsets table	
	std::vector<uint32_t> trackOffsets;
	trackOffsets.resize(168);

	if (!ReadFile(m_file, trackOffsets.data(), (DWORD)(sizeof(uint32_t) * trackOffsets.size()), &read, NULL)) read = 0;
	if (read != sizeof(uint32_t) * trackOffsets.size()) return false;
	
	// Prepare track areas
	for (uint32_t trk = m_firstTrack; trk <= m_lastTrack; trk++) {
		Track t;
		t.lastRev = 0;
		t.m_fileOffset = trackOffsets[trk];
		t.m_trackIsBad = false;
		m_tracks.insert(std::pair(trk, t));
	}

	// Decode a few tracks to check if its valid
	if (!decodeTrack(m_firstTrack)) return false;
	if (!decodeTrack(m_lastTrack)) return false;

	return true;
} 

// Decode a specific track into MFM
bool SCPFile::decodeTrack(uint32_t track) {
	if (track < m_firstTrack) return false;
	if (track > m_lastTrack) return false;

	auto trk = m_tracks.find(track);
	if (trk == m_tracks.end()) return false;

	if (!trk->second.revolutions.empty()) return true;
	if (trk->second.m_trackIsBad) return false;

	// Temporarly mark the track as bad
	trk->second.m_trackIsBad = true;

	// This means no data
	if (trk->second.m_fileOffset == 0) return false;

	// Goto the track data
	if (SetFilePointer(m_file, trk->second.m_fileOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return false;

	// Read track header and validate it
	SCPTrackHeader header;
	DWORD read;
	if (!ReadFile(m_file, &header, sizeof(header), &read, NULL)) read = 0;
	if ((read != sizeof(header)) || ((header.headerTRK[0] != 'T') || (header.headerTRK[1] != 'R') || (header.headerTRK[2] != 'K'))) return false;
	if (header.trackNumber != track) return false;

	std::vector<SCPTrackRevolution> revolutions;
	// Now read in the track info - the start of each revolution
	for (uint32_t r = 0; r < m_numRevolutions; r++) {
		SCPTrackRevolution rev;
		if (!ReadFile(m_file, &rev, sizeof(rev), &read, NULL)) read = 0;
		if (read != sizeof(rev)) return false;
		revolutions.push_back(rev);
	}

	// Temp buffer for flux timing data
	std::vector<uint16_t> data;

	m_pll.reset();

	// Quick scan for density - can't rely on the header information being correct as it quite often isnt!
	if (!m_density) {
		// This is for guessing DD vs HD data
		uint32_t ns2 = 0;
		uint32_t ns6 = 0;		

		for (const SCPTrackRevolution& rev : revolutions) {
			if (SetFilePointer(m_file, rev.dataOffset + trk->second.m_fileOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return false;
			// Read in the RAW flux timing
			data.resize(rev.trackLength);
			if (!ReadFile(m_file, (char*)data.data(), rev.trackLength * 2, &read, NULL)) read = 0;
			if (read != rev.trackLength * 2) return false;

			uint32_t lastTime = 0;
			for (const uint16_t t : data) {
				const uint16_t t2 = htons(t);  // paws naidne
				if (t2 == 0) lastTime += 65536; else {
					const uint32_t totalFlux = (lastTime + t2) * m_fluxMultiplier;
					if (totalFlux < 2500) ns2++;  // High Density 01 would be 2000ns
					if (totalFlux > 5000) ns6++;  // Double density 001 would be 6000ns
					lastTime = 0;
				}
			}
		}
		m_density = ns2 > ns6 ? 2 : 1;
	}

	// Now read in and decode each track, directly into MFM
	for (const SCPTrackRevolution& rev : revolutions) {
		m_pll.newTrack();

		if (SetFilePointer(m_file, rev.dataOffset + trk->second.m_fileOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return false;

		// Read in the RAW flux timing
		data.resize(rev.trackLength);
		if (!ReadFile(m_file, (char*)data.data(), rev.trackLength * 2, &read, NULL)) read = 0;
		if (read != rev.trackLength * 2) return false;

		// Convert data into MFM
		uint32_t lastTime = 0;
		for (const uint16_t t : data) {
			const uint16_t t2 = htons(t);  // paws naidne
			if (t2 == 0) lastTime += 65536; else {
				const uint32_t totalFlux = (lastTime + t2) * m_fluxMultiplier;
				m_pll.decodeFlux(totalFlux * m_density);
				lastTime = 0;
			}
		}

		// Have a look at what we got
		void* memory = nullptr;
		uint32_t sizeInBits = m_pll.finaliseTrack(&memory);

		// Make enough memory
		trk->second.revolutions.push_back(Revolution());		
		trk->second.revolutions.back().sizeInBits = sizeInBits;
		trk->second.revolutions.back().mfmData.resize((sizeInBits + 7) / 8);

		// Save it
		memcpy_s(&trk->second.revolutions.back().mfmData[0], trk->second.revolutions.back().mfmData.size(), memory, (sizeInBits + 7) / 8);
	}

	trk->second.m_trackIsBad = trk->second.revolutions.empty();

	return !trk->second.m_trackIsBad;
}


// Flags from WINUAE
SCPFile::SCPFile(HANDLE file, std::function<void(bool diskInserted, SectorType diskFormat)> diskChangeCallback) 
	: SectorCacheMFM(diskChangeCallback), m_file(file) {

	if (!readSCPFile()) m_tracks.clear();

	if (m_tracks.size()) setReady();
}


SCPFile::~SCPFile() {
	quickClose();
}

// Rapid shutdown
void SCPFile::quickClose() {
	if (m_file != INVALID_HANDLE_VALUE) {
		CloseHandle(m_file);
		m_file = INVALID_HANDLE_VALUE;
	}
}


// Return TRUE if it loaded OK
bool SCPFile::isDiskInDrive() {
	return !m_tracks.empty();
}

// Extract (and decode)
uint32_t SCPFile::mfmRead(uint32_t track, bool retryMode, void* data, uint32_t maxLength) {
	if (!decodeTrack(track)) return 0;
	
	// Does the track exist?
	auto i = m_tracks.find(track);
	if (i == m_tracks.end()) return 0;
	if (i->second.revolutions.size() < 1) return 0;

	uint32_t rev = i->second.lastRev;
	// Goto the next revolution next time
	i->second.lastRev = (i->second.lastRev + 1) % i->second.revolutions.size();

	// copy in the current revolution of data
	memcpy_s(data, maxLength, i->second.revolutions[rev].mfmData.data(), min(maxLength, i->second.revolutions[rev].mfmData.size()));

	// Calc the number of bits we *can* store
	int32_t bitsRemaining = (int32_t)(maxLength * 8) - (int32_t)i->second.revolutions[rev].sizeInBits;
	if (bitsRemaining<=0) return maxLength * 8;
		
	// Space left, see if the data is byte aligned to a byte or not so we can try to fill it from the NEXT revolution
	const uint32_t bitsMismatchRemaining = bitsRemaining & 7;

	// Is data byte aligned?
	if (bitsMismatchRemaining == 0) {
		uint8_t* outputByte = (uint8_t*)data;
		const uint32_t spaceUsed = i->second.revolutions[rev].sizeInBits / 8;
		outputByte += spaceUsed;
		const uint32_t bytesToCopy = min(maxLength - spaceUsed, (uint32_t)i->second.revolutions[rev].mfmData.size());
		memcpy_s(outputByte, maxLength - spaceUsed, i->second.revolutions[rev].mfmData.data(), bytesToCopy);

		// Return how much we actually copied
		return (spaceUsed + bytesToCopy) * 8;
	}
	else {
		uint8_t* outputByte = (uint8_t*)data;
		outputByte += i->second.revolutions[rev].sizeInBits / 8;

		// Not aligned.
		for (const uint8_t byte : i->second.revolutions[i->second.lastRev].mfmData) {

			// mis-aligned output
			*outputByte |= byte >> (8 - bitsMismatchRemaining);
			outputByte++;
			bitsRemaining -= 8;
			if (bitsRemaining <= 0) return maxLength * 8;

			// Prepare next byte
			*outputByte = byte << bitsMismatchRemaining;
		}

		// This shouldn't really ever get reached
		return (maxLength * 8) - bitsRemaining;
	}
}