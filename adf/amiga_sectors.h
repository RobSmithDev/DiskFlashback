#pragma once

// The code in these files was taken from ADFWriter which is part of the DrawBridge - aka ArduinoFloppyReader (and writer) code
// ... also by me!
#include <Windows.h>
#include <stdint.h>
#include <unordered_map>

#define SECTOR_BYTES				512			// Number of bytes in a decoded sector
#define NUM_SECTORS_PER_TRACK_DD	11			// Number of sectors per track
#define NUM_SECTORS_PER_TRACK_HD	22			// Same but for HD disks

typedef unsigned char RawDecodedSector[SECTOR_BYTES];

// Structure to hold data while we decode it
typedef struct {	
	uint32_t numErrors;					// Number of decoding errors found
	RawDecodedSector data;          // decoded sector data
} DecodedSector;

// To hold a list of valid and checksum failed sectors
struct DecodedTrack {
	// A map of sector number to valid sectors 
	std::unordered_map<int, DecodedSector> sectors;

	uint32_t sectorsWithErrors;
};

// Searches for sectors - you can re-call this and it will update decodedTrack rather than replace it
void findSectors(const unsigned char* track, const uint32_t dataLengthInBits, const bool isHD, const unsigned int trackNumber, DecodedTrack& decodedTrack);

// Encodes all sectors into the buffer provided and returns the number of bytes that need to be written to disk 
// mfmBufferSizeBytes needs to be at least 13542 or DD and 27076 for HD
uint32_t encodeSectorsIntoMFM(const bool isHD, const DecodedTrack& decodedTrack, const uint32_t trackNumber, const uint32_t mfmBufferSizeBytes, void* memBuffer);
