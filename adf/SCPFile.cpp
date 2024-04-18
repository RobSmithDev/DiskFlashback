
#include "SCPFile.h"


#pragma pack(1) 

/* Taken from https://www.cbmstuff.com/downloads/scp/scp_image_specs.txt
This information is copyright(C) 2012 - 2020 By Jim Drew. Permission is granted
for inclusion with any source code when keeping this copyright notice.
*/
struct SCPFileHeader {
	char			headerSCP[3];
	unsigned char	version;
	unsigned char	diskType;
	unsigned char	numRevolutions;
	unsigned char	startTrack;
	unsigned char   endTrack;
	unsigned char	flags;
	unsigned char	bitcellEncoding;   // 0=16 bits per sample, 
	unsigned char	numHeads;
	unsigned char   timeBase;          // Resolution. Time in ns = (timeBase+1)*25
	uint32_t	checksum;
};

struct SCPTrackHeader {
	char			headerTRK[3];
	unsigned char	trackNumber;
};

struct SCPTrackRevolution {
	uint32_t	indexTime;		// Time in NS/25 for this revolution
	uint32_t	trackLength;	// Number of bit-cells in this revolution
	uint32_t	dataOffset;		// From the start of SCPTrackHeader 
};

// Track data is 16-bit value in NS/25.  If =0 means no flux transition for max time 
#pragma pack()

#define BITFLAG_INDEX		0
#define BITFLAG_96TPI		1
#define BITFLAG_NORMALISED  3
#define BITFLAG_EXTENDED    6
#define BITFLAG_FLUXCREATOR 7

typedef std::vector<uint16_t> SCPTrackData;

struct SCPTrackInMemory {
	SCPTrackHeader header;
	std::vector<SCPTrackRevolution> revolution;
	std::vector<SCPTrackData> revolutionData;
};

// Actually read the file
bool SCPFile::readSCPFile(HANDLE file) {
	DWORD read;
	SCPFileHeader header;
	SetFilePointer(file, 0, NULL, FILE_BEGIN);
	if (!ReadFile(file, &header, sizeof(header), &read, NULL)) read = 0;
	if (read != sizeof(header)) return false;

	const DWORD fluxMultiplier = (header.timeBase + 1) * 25;
	numHeads = header.numHeads;

	// Read the offsets table
	std::vector<uint32_t> trackOffsets;
	trackOffsets.resize(168);

	if (!ReadFile(file, trackOffsets.data(), sizeof(uint32_t) * trackOffsets.size(), &read, NULL)) read = 0;
	if (read != sizeof(uint32_t) * trackOffsets.size()) return false;		

	// Read in all the tracks
	for (uint32_t track = header.startTrack; track <= header.endTrack; track++) {
		// Find the track data
		if (SetFilePointer(file, trackOffsets[track], NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return false;

		// Read track header
		SCPTrackInMemory trk;
		if (!ReadFile(file, &trk.header, sizeof(trk.header), &read, NULL)) read = 0;
		if ((read != sizeof(trk.header)) || ((trk.header.headerTRK[0] != 'T') || (trk.header.headerTRK[1] != 'R') || (trk.header.headerTRK[2] != 'K'))) return false;

		// Now read in the track info - the start of each revolution
		for (int r = 0; r < header.numRevolutions; r++) {
			SCPTrackRevolution rev;
			if (!ReadFile(file, &rev, sizeof(rev), &read, NULL)) read = 0;
			if (read != sizeof(rev)) return false;
			trk.revolution.push_back(rev);
		}

		SCPTrackData allData;
		// And now read in their data
		for (uint32_t r = 0; r < header.numRevolutions; r++) {
			// Goto the data
			std::vector<uint16_t> actualFluxTimes;
			if (SetFilePointer(file, trk.revolution[r].dataOffset + trackOffsets[track], NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return false;

			allData.resize(trk.revolution[r].trackLength);
			trk.revolution[r].dataOffset = actualFluxTimes.size();  // for use later on

			if (!ReadFile(file, (char*)allData.data(), trk.revolution[r].trackLength * 2, &read, NULL)) read = 0;
			if (read != trk.revolution[r].trackLength * 2) return false;

			// Convert allData into proper flux times in nanoseconds
			DWORD lastTime = 0;
			for (const uint16_t t : allData) {
				const uint16_t t2 = htons(t);  // paws naidne
				if (t2 == 0) lastTime += 65536; else {
					DWORD totalFlux = (lastTime + t2) * fluxMultiplier;
					actualFluxTimes.push_back(totalFlux);
					lastTime = 0;
				}
			}

			trk.revolution[r].trackLength = actualFluxTimes.size() - trk.revolution[r].dataOffset;

			TrackRev rev2;
			rev2.mfmData = actualFluxTimes;

			// Find and insert
			auto i = m_tracks.find(track);
			if (i == m_tracks.end()) {
				Track trk;
				trk.revs.push_back(rev2);
				m_tracks.insert(std::pair(track, trk));
			}
			else i->second.revs.push_back(rev2);
		}
	}
	return true;
} 

// Flags from WINUAE
SCPFile::SCPFile(HANDLE file, std::function<void(bool diskInserted, SectorType diskFormat)> diskChangeCallback) 
	: SectorCacheMFM(diskChangeCallback) {

	if (!readSCPFile(file)) m_tracks.clear();

	checkHD();

	CloseHandle(file);

	if (m_tracks.size()) setReady();
}


// Return TRUE if it loaded OK
bool SCPFile::isDiskInDrive() {
	return !m_tracks.empty();
}

// Scan if the data looks like HD data
void SCPFile::checkHD() {
	if (m_tracks.empty()) return;
	auto i = m_tracks.begin();

	// look at the data
	uint32_t ns2 = 0;
	uint32_t ns6 = 0;
	for (uint16_t fluxTime : i->second.revs[0].mfmData) {
		if (fluxTime < 3000) ns2++;
		if (fluxTime > 5000) ns6++;
	}

	m_hd = ns2 > ns6;
}

// Extract (and decode)
uint32_t SCPFile::mfmRead(uint32_t cylinder, bool upperSide, bool retryMode, void* data, uint32_t maxLength) {
	const uint32_t track = (cylinder * numHeads) + (upperSide ? 1 : 0);
	
	// Does the track exist?
	auto i = m_tracks.find(track);
	if (i == m_tracks.end()) return 0;
	if (i->second.revs.size() < 1) return 0;

	uint32_t rev = i->second.lastRev;

	m_pll.newTrack(data, maxLength);
	int counter = 0;

	uint32_t remaining = 0;
	for (uint32_t counter=0; counter<10; counter++) {
		remaining = m_pll.decodeFlux(i->second.revs[rev].mfmData);
		rev = (rev + 1) % i->second.revs.size();
		if (remaining < 1) return maxLength * 2;
		counter++;
	}

	i->second.lastRev = (i->second.lastRev + 1) % i->second.revs.size();
	return (maxLength*8)-remaining;
}