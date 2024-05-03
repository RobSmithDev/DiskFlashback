#pragma once

#define MFM_MASK					0x55555555L		
#define DEFAULT_SECTOR_BYTES		512				// Number of bytes in a decoded sector - the default, but NOT always
#define MAX_TRACK_SIZE				(0x3A00 * 2)	// used for MFM encoding etc

#include <map>

typedef std::vector<uint8_t> RawDecodedSector;

// Structure to hold data while we decode it
typedef struct {
	uint32_t numErrors;					// Number of decoding errors found
	RawDecodedSector data;          // decoded sector data
} DecodedSector;

// To hold a list of valid and checksum failed sectors
struct DecodedTrack {
	// A map of sector number to valid sectors 
	std::map<int, DecodedSector> sectors;

	uint32_t sectorsWithErrors;
};

