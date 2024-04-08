// fat12decode.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <dokan/dokan.h>
#include <iostream>
#include <vector>
#include <unordered_map>
#include "ibm_sectors.h"

#define IBM_DD_SECTORS 9
#define IBM_HD_SECTORS 18

// IAM A1A1A1FC
#define MFM_SYNC_TRACK_HEADER				0x5224522452245552ULL
// IDAM A1A1A1FE 
#define MFM_SYNC_SECTOR_HEADER				0x4489448944895554ULL
// DAM A1A1A1FB (data address mark)
#define MFM_SYNC_SECTOR_DATA				0x4489448944895545ULL
// DDAM A1A1A1F8 (deleted data address mark)
#define MFM_SYNC_DELETED_SECTOR_DATA        0x448944894489554AULL

// IAM Header data structore
typedef struct alignas(1)  {
	unsigned char addressMark[4]; // should be 0xA1A1A1FE
	unsigned char cylinder;
	unsigned char head;
	unsigned char sector;
	unsigned char length;  // 2^(length+7) sector size = should be 2 for 512
	unsigned char crc[2];	
} IBMSectorHeader;

// IDAM data
typedef struct {
	unsigned char dataMark[4]; // should be 0xA1A1A1FB
	std::vector<unsigned char> data; // *should* be 512 but doesn't have to be
	unsigned char crc[2];
} IBMSectorData;

typedef struct {
	IBMSectorHeader header;
	IBMSectorData data;
	uint32_t headerErrors;   // number of errors in the header. 0 means PERFECT!
	bool dataValid;
} IBMSector;

typedef struct {
	// mapping of sector number to actual sector data
	std::unordered_map<uint32_t, IBMSector> sectors;
	uint32_t numErrors;
} IBMTrack;


// Simple byte swap
inline uint16_t wordSwap(uint16_t w) {
	return (w << 8) | (w >> 8);
}

// Very nasty, I'm sure theres a better way than this
uint32_t encodeMFMdata(const uint8_t* input, uint8_t* output, const uint32_t inputSize, uint8_t& lastByte, uint8_t* memOverflow) {

	bool lastBit = lastByte & 1;
	for (uint32_t b = 0; b < inputSize; b++) {
		if (output >= memOverflow) return max(((int32_t)b) - 1, 0) << 1;

		uint8_t byte = *input++;
		for (uint32_t bit = 0; bit < 8; bit++) {
			*output <<= 2;
			if (byte & 0x80) {
				*output |= 1;
				lastBit = true;
			}
			else {
				if (!lastBit) *output |= 2;
				lastBit = false;
			}
			// Move to next bit
			byte <<= 1;
			if (bit == 3) {
				output++;
				if (output >= memOverflow) return b << 1;
			}
		}
		output++;
	}
	lastByte = *(input - 1);
	return inputSize << 1; 
}

// CRC16
uint16_t crc16(char* pData, int length, uint32_t wCrc = 0xFFFF) {
	uint8_t i;
	while (length--) {
		wCrc ^= *(unsigned char*)pData++ << 8;
		for (i = 0; i < 8; i++)
			wCrc = wCrc & 0x8000 ? (wCrc << 1) ^ 0x1021 : wCrc << 1;
	}
	return wCrc & 0xffff;
}

// Extract the data, properly aligned into the output
void extractMFMDecodeRaw(const unsigned char* inTrack, const uint32_t dataLengthInBits, const uint32_t bitPos, uint32_t outputBytes, uint8_t* output) {
	unsigned char byteOut = 0;
	unsigned int byteOutPosition = 0;

	uint32_t realBitPos = bitPos+1;  // the +1 skips past the clock bit

	unsigned char* memOut = output;

	while (outputBytes) {
		for (uint32_t bit = 0; bit <= 7; bit++) {
			*memOut <<= 1;
			const uint32_t trackBytePos = realBitPos >> 3;
			const uint32_t trackBitPos = 7 - (realBitPos & 7);
			if (inTrack[trackBytePos] & (1 << trackBitPos)) *memOut |= 1;
			realBitPos = (realBitPos + 2) % dataLengthInBits;  // skip those clock bits
		}
		// Move along and reset
		memOut++;
		outputBytes--;
	}
}

// Searches for sectors - you can re-call this and it will update decodedTrack rather than replace it
// nonstandardTimings is set to true if this uses non-standard timings like those used by Atari etc
void findSectors_IBM(const uint8_t* track, const uint32_t dataLengthInBits, const bool isHD, const uint32_t trackNumber, const uint32_t expectedNumSectors, DecodedTrack& decodedTrack, bool& nonstandardTimings) {
	const uint32_t cylinder = trackNumber / 2;
	const bool upperSide = trackNumber & 1;

	// Prepare our test buffer
	uint64_t decoded = 0;

	int nextTrackBitCount = 0;
	int i = 0;
	IBMSector sector;

	bool headerFound = false;
	sector.headerErrors = 0xFFFF;
	sector.dataValid = false;
	int8_t lastSectorNumber = -1;
	uint8_t sectorSize = 2; // default - 512
	nonstandardTimings = false;

	uint32_t expectedSectors = expectedNumSectors ? expectedNumSectors : (isHD ? IBM_HD_SECTORS : IBM_DD_SECTORS);

	uint32_t sectorEndPoint = 0;

	// run the entire track length with some space to wrap around
	for (uint32_t bit = 0; bit < dataLengthInBits; bit++) {
		const uint32_t realBitPos = bit % dataLengthInBits;
		const uint32_t trackBytePos = realBitPos >> 3;
		const uint32_t trackBitPos = 7 - (realBitPos & 7);
		decoded <<= 1ULL;   // shift off one bit to make room for the new bit
		if (track[trackBytePos] & (1 << trackBitPos)) decoded |= 1;

		if (decoded == MFM_SYNC_SECTOR_HEADER) {
			// Grab sector header
			if (sectorEndPoint) {
				uint32_t markerStart = bit + 1 - 64;
				uint32_t bytesBetweenSectors = markerStart / 8;
				char buf[100];
				sprintf_s(buf, "Bytes: %i\n", bytesBetweenSectors);
				OutputDebugStringA(buf);
			}

			extractMFMDecodeRaw(track, dataLengthInBits, bit + 1 - 64, sizeof(sector.header), (uint8_t*)&sector.header);
			uint16_t crc = crc16((char*)&sector.header, sizeof(sector.header) - 2);
			headerFound = true;
			sector.headerErrors = 0;
			if (crc != wordSwap(*(uint16_t*)sector.header.crc)) sector.headerErrors++;
			if (!sector.headerErrors) sectorSize = sector.header.length;
			if (sector.header.cylinder != cylinder) sector.headerErrors++;
			if (sector.header.head != (upperSide ? 1 : 0)) sector.headerErrors++;
			lastSectorNumber = sector.header.sector;
		}
		else
			if (decoded == MFM_SYNC_SECTOR_DATA) {
				if (!headerFound) {
					// Sector header was missing. We'll "guess" one - not ideal!
					lastSectorNumber++;
					memset(&sector.header, 0, sizeof(sector.header));
					sector.header.sector = (uint8_t)lastSectorNumber;
					sector.header.length = sectorSize;
					sector.header.cylinder = cylinder;
					sector.header.head = upperSide ? 1 : 0;
					sector.headerErrors = 0xF0;
				}
				if (headerFound) {
					const uint32_t sectorDataSize = 1 << (7 + sector.header.length);
					if (sector.data.data.size() != sectorDataSize) sector.data.data.resize(sectorDataSize);
					uint32_t bitStart = bit + 1 - 64;
					// Extract the header section
					extractMFMDecodeRaw(track, dataLengthInBits, bitStart, 4, (uint8_t*)&sector.data.dataMark);
					// Extract the sector data
					bitStart += 4 * 8 * 2;
					extractMFMDecodeRaw(track, dataLengthInBits, bitStart, (uint32_t)sector.data.data.size(), (uint8_t*)&sector.data.data[0]);
					// Extract the sector CRC
					bitStart += sectorDataSize * 8 * 2;
					extractMFMDecodeRaw(track, dataLengthInBits, bitStart, 2, (uint8_t*)&sector.data.crc);
					// Validate
					uint16_t crc = crc16((char*)&sector.data, 4);
					crc = crc16((char*)sector.data.data.data(), (uint32_t)sector.data.data.size(), crc);
					sector.dataValid = crc == wordSwap(*(uint16_t*)sector.data.crc);

					// Standardize the sector
					DecodedSector sec;
					sec.data = sector.data.data;
					sec.numErrors = sector.headerErrors + sector.dataValid ? 1 : 0;

					// See if this already exists 
					auto it = decodedTrack.sectors.find(sector.header.sector);
					if (it == decodedTrack.sectors.end()) {
						decodedTrack.sectors.insert(std::make_pair(sector.header.sector, sec));
					}
					else {
						// Does exist. Keep the better copy
						if (it->second.numErrors > sec.numErrors) {
							it->second.data = sec.data;
							it->second.numErrors = sec.numErrors;
						}						
					}

					// Reset for next sector
					sector.headerErrors = 0xFFFF;
					sector.dataValid = false;
					headerFound = false;
					sectorEndPoint = bitStart + 4;  // mark the end of the sector
				}
			}
			else
				if (decoded == MFM_SYNC_TRACK_HEADER) {
					// Reset here, not reqally required, but why not!
					headerFound = false;
					sector.headerErrors = 0xFFFF;
					sector.dataValid = false;
					lastSectorNumber = -1;
				}
	}

	const uint32_t sectorDataSize = 1 << (7 + sector.header.length);

	// Add dummy sectors upto expectedSectors
	decodedTrack.sectorsWithErrors = 0;
	for (uint32_t sec = 1; sec <= expectedSectors; sec++) {
		auto it = decodedTrack.sectors.find(sec);

		// Does a sector with this number exist?
		if (it == decodedTrack.sectors.end()) {
			if (expectedNumSectors) {
				// No. Create a dummy one - VERY NOT IDEAL!
				DecodedSector tmp;
				tmp.data.resize(sectorDataSize);
				memset(&tmp.data[0], 0, sizeof(tmp));
				tmp.numErrors = 0xFFFF;
				decodedTrack.sectors.insert(std::make_pair(sec, tmp));
				decodedTrack.sectorsWithErrors++;
			}
		}
		else
			if (it->second.numErrors) decodedTrack.sectorsWithErrors++;
	}
}
// Find sectors (one less parameter)
void findSectors_IBM(const uint8_t* track, const uint32_t dataLengthInBits, const bool isHD, const uint32_t trackNumber, const uint32_t expectedNumSectors, DecodedTrack& decodedTrack) {
	bool tmp;
	findSectors_IBM(track, dataLengthInBits, isHD, trackNumber, expectedNumSectors, decodedTrack, tmp);
}

// The fill is 0x4E, which endoded as MFM is
uint32_t gapFillMFM(uint8_t* mem, const uint32_t size, const uint8_t value, uint8_t& lastByte, uint8_t* memOverflow) {
	std::vector<uint8_t> data;
	if (size < 1) return 0;
	data.resize(size, value);
	return encodeMFMdata(data.data(), mem, (uint32_t)data.size(), lastByte, memOverflow);
}

// The fill is 0x4E, which endoded as MFM is
uint32_t writeRawMFM(uint8_t* mem, const uint32_t size, const uint8_t value, uint8_t& lastByte, uint8_t* memOverflow) {
	if (size<1) return 0;
	if (mem >= memOverflow) return 0;
	if ((lastByte & 1) && (value & 0x80)) *mem = value & 0x7F; else *mem = value;
	mem++;
	for (uint8_t i = 1; i < size; i++) {
		if (mem >= memOverflow) return i;
		*mem = value;
		mem++;
	}
	lastByte = value;
	return size;
}

// The fill is 0x4E, which endoded as MFM is
uint32_t writeMarkerMFM(uint8_t* mem, const uint64_t value, uint8_t& lastByte, uint8_t* memOverflow) {
	uint64_t v = value;
	for (uint32_t byte = 0; byte < 8; byte++) {
		if (mem >= memOverflow) return byte;
		for (uint32_t bit = 0; bit < 8; bit++) {
			*mem <<= 1;
			if (v & 0x8000000000000000ULL) *mem |= 1;
			v <<= 1ULL;
		}
		mem++;
	}
	lastByte = value & 0xFF;
	return 8;
}

// Encode the track supplied into a raw MFM bit-stream
uint32_t encodeSectorsIntoMFM_IBM(const bool isHD, const bool forceAtariTiming, DecodedTrack* decodedTrack, const uint32_t trackNumber, uint32_t mfmBufferSizeBytes, void* trackData) {
	uint8_t lastByte = 0x55;
	const uint32_t cylinder = trackNumber / 2;
	const bool upperSide = trackNumber & 1;

	// Gaps thats we need to create - this is the default for a "normal" disk
	uint8_t gap4aSize = 80;    // 0x4E - this is BEFORE the track header
	uint8_t gap1Size =  50;    // 0x4E - this is AFTER the track header
	uint8_t gap2Size =  22;    // 0x4E - between sector header and data
	uint8_t gap3Size =  84;    // 0x4E - after data sector
	uint8_t gap4bSize = 182;   // 0x4E - after all sectors
	bool writeTrackHeader = true;
	
	if (decodedTrack->sectors.size() > 21) return 0;

	// NOTE: ALL OF THE ATARI TIMINGS NEED CHECKING!
	if (forceAtariTiming) {
		gap4aSize = 80;   
		gap1Size = 60;    
		gap2Size = 22;    
		gap3Size = 40;    
		gap4bSize = 60;  
	}

	switch (decodedTrack->sectors.size()) {
	case 10: // double density atari
		gap3Size = 40;
		break;
	case 11: // double density atari
		gap1Size = 10;
		gap3Size = 1;
		gap4bSize = 0;
		break;
	case 19: 
		gap3Size = 26;
		break;
	case 20:
		gap3Size = 20;
		break;
	case 21:
		gap1Size = 10;
		gap3Size = 1;
		gap4bSize = 1;
		break;	
	case 22:
		gap1Size = 1;   
		gap3Size = 1;
		gap4aSize = 1;
		gap4bSize = 1;
		gap2Size = 4;
		break;
	}

	uint8_t* mem = (uint8_t*)trackData;
	uint8_t* memOverflow = mem + mfmBufferSizeBytes;
	
	mem += gapFillMFM(mem, gap4aSize, 0x4E, lastByte, memOverflow);

	mem += writeRawMFM(mem, 24, 0xAA, lastByte, memOverflow);
	mem += writeMarkerMFM(mem, MFM_SYNC_TRACK_HEADER, lastByte, memOverflow);
	mem += gapFillMFM(mem, gap1Size, 0x4E, lastByte, memOverflow);
	for (uint32_t sec = 1; sec <= decodedTrack->sectors.size(); sec++) {
		const DecodedSector& sector = decodedTrack->sectors[sec];

		mem += writeRawMFM(mem, 24, 0xAA, lastByte, memOverflow);

		IBMSectorHeader header;  // this isn't strictly whats written though
		header.addressMark[0] = 0xA1;
		header.addressMark[1] = 0xA1;
		header.addressMark[2] = 0xA1;
		header.addressMark[3] = 0xFE;
		header.cylinder = cylinder;
		header.head = upperSide ? 1 : 0;
		header.sector = sec;
		header.length = (unsigned char)(max(0,log2(sector.data.size()) - 7));
		*((uint16_t*)header.crc) = wordSwap(crc16((char*)&header, sizeof(header) - 2));

		mem += writeMarkerMFM(mem, MFM_SYNC_SECTOR_HEADER, lastByte, memOverflow);
		mem += encodeMFMdata(((uint8_t*)&header)+4, mem, sizeof(header) - 4, lastByte, memOverflow);
		mem += 4;
		mem += gapFillMFM(mem, gap2Size, 0x4E, lastByte, memOverflow);

		mem += writeRawMFM(mem, 24, 0xAA, lastByte, memOverflow);
		// Need this just for the CRC
		const uint8_t dataMark[4] = { 0xA1, 0xA1, 0xA1, 0xFB };
		uint16_t crc = crc16((char*)&dataMark, 4);
		crc = wordSwap(crc16((char*)sector.data.data(), (int)sector.data.size(), crc));

		mem += writeMarkerMFM(mem, MFM_SYNC_SECTOR_DATA, lastByte, memOverflow);
		mem += encodeMFMdata(sector.data.data(), mem, (uint32_t)sector.data.size(), lastByte, memOverflow);
		mem += encodeMFMdata((uint8_t*)&crc, mem, sizeof(crc), lastByte, memOverflow);

		mem += gapFillMFM(mem, gap3Size, 0x4E, lastByte, memOverflow);
	}
	// dont care to write this
	//mem += gapFillMFM(mem, gap4bSize, 0x4E, lastByte);
	mem += gapFillMFM(mem, gap4bSize/2, 0x4E, lastByte, memOverflow);
	return (uint32_t)(mem - ((uint8_t*)trackData));
}

// Feed in Track 0, sector 0 and this will try to extract the number of sectors per track, or 0 on error
bool getTrackDetails_IBM(const uint8_t* sector, uint32_t& serialNumber, uint32_t& sectorsPerTrack, uint32_t& bytesPerSector) {
	sectorsPerTrack = 0;
	serialNumber = 0;

	if (!sector) return false;

	serialNumber = (sector[0x08 + 3] << 24) | (sector[0x08 + 2] << 16) | (sector[0x08 + 1] << 8) | sector[0x08];
	sectorsPerTrack = (sector[0x18 + 1] << 8) | sector[0x18];
	if (sectorsPerTrack < 3) return false;
	if (sectorsPerTrack > 22) return false;
	bytesPerSector = (sector[0x0B + 1] << 8) | sector[0x0B];
	return ((bytesPerSector == 128) || (bytesPerSector == 256) || (bytesPerSector == 512) || (bytesPerSector == 1024) || (bytesPerSector == 2048) || (bytesPerSector == 4096));
}


// Feed in Track 0, sector 0 and this will try to extract the number of sectors per track, or 0 on error
bool getTrackDetails_IBM(const DecodedTrack* decodedTrack, uint32_t& serialNumber, uint32_t& sectorsPerTrack, uint32_t& bytesPerSector) {
	sectorsPerTrack = 0;
	serialNumber = 0;

	if (!decodedTrack) return false;
	auto it = decodedTrack->sectors.find(1);
	if (it == decodedTrack->sectors.end()) return false;
	if (it->second.data.size() < 128) return false;
	return getTrackDetails_IBM(it->second.data.data(), serialNumber, sectorsPerTrack, bytesPerSector);
}
