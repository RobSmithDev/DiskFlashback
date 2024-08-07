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



#include "amiga_sectors.h"

#define NUM_SECTORS_PER_TRACK_DD	11			// Number of sectors per track
#define NUM_SECTORS_PER_TRACK_HD	22			// Same but for HD disks
#define SECTOR_BYTES DEFAULT_SECTOR_BYTES
#define AMIGA_WORD_SYNC  0x4489							 // Disk SYNC code for the Amiga start of sector
#define RAW_SECTOR_SIZE (8+56+SECTOR_BYTES+SECTOR_BYTES)      // Size of a sector, *Including* the sector sync word longs
#define ADF_TRACK_SIZE_DD (SECTOR_BYTES*NUM_SECTORS_PER_TRACK_DD)   // Bytes required for a single track dd
#define ADF_TRACK_SIZE_HD (SECTOR_BYTES*NUM_SECTORS_PER_TRACK_HD)   // Bytes required for a single track hd
#define PRE_FILLER 1654


typedef unsigned char RawEncodedSector[RAW_SECTOR_SIZE];


typedef struct alignas(8) {
	unsigned char trackFormat;        // This will be 0xFF for Amiga
	unsigned char trackNumber;        // Current track number (this is actually (tracknumber*2) + side
	unsigned char sectorNumber;       // The sector we just read (0 to 11)
	unsigned char sectorsRemaining;   // How many more sectors remain until the gap (0 to 10)
} SectorHeader;


// These were borrowed from the WinUAE source code, HOWEVER, they're identical to what the Amiga OS writes with the INSTALL command
static uint8_t bootblock_ofs[] = {
	0x44,0x4f,0x53,0x00,0xc0,0x20,0x0f,0x19,0x00,0x00,0x03,0x70,0x43,0xfa,0x00,0x18,
	0x4e,0xae,0xff,0xa0,0x4a,0x80,0x67,0x0a,0x20,0x40,0x20,0x68,0x00,0x16,0x70,0x00,
	0x4e,0x75,0x70,0xff,0x60,0xfa,0x64,0x6f,0x73,0x2e,0x6c,0x69,0x62,0x72,0x61,0x72,
	0x79
};
static uint8_t bootblock_ffs[] = {
	0x44, 0x4F, 0x53, 0x01, 0xE3, 0x3D, 0x0E, 0x72, 0x00, 0x00, 0x03, 0x70, 0x43, 0xFA, 0x00, 0x3E,
	0x70, 0x25, 0x4E, 0xAE, 0xFD, 0xD8, 0x4A, 0x80, 0x67, 0x0C, 0x22, 0x40, 0x08, 0xE9, 0x00, 0x06,
	0x00, 0x22, 0x4E, 0xAE, 0xFE, 0x62, 0x43, 0xFA, 0x00, 0x18, 0x4E, 0xAE, 0xFF, 0xA0, 0x4A, 0x80,
	0x67, 0x0A, 0x20, 0x40, 0x20, 0x68, 0x00, 0x16, 0x70, 0x00, 0x4E, 0x75, 0x70, 0xFF, 0x4E, 0x75,
	0x64, 0x6F, 0x73, 0x2E, 0x6C, 0x69, 0x62, 0x72, 0x61, 0x72, 0x79, 0x00, 0x65, 0x78, 0x70, 0x61,
	0x6E, 0x73, 0x69, 0x6F, 0x6E, 0x2E, 0x6C, 0x69, 0x62, 0x72, 0x61, 0x72, 0x79, 0x00, 0x00, 0x00,
};



// Grabs a copy of the bootblock for the system required.  target must be 1024 bytes in size
void fetchBootBlockCode_AMIGA(bool ffs, uint8_t* target) {
	if (ffs) {
		size_t size = sizeof(bootblock_ofs) / sizeof(*bootblock_ofs);
		memcpy_s(target, 1024, bootblock_ofs, size);
	}
	else {
		size_t size = sizeof(bootblock_ffs) / sizeof(*bootblock_ffs);
		memcpy_s(target, 1024, bootblock_ffs, size);
	}
}



// Very simple 
void getTrackDetails_AMIGA(const bool isHD, uint32_t& sectorsPerTrack, uint32_t& bytesPerSector) {
	sectorsPerTrack = isHD ? NUM_SECTORS_PER_TRACK_HD : NUM_SECTORS_PER_TRACK_DD;
	bytesPerSector = SECTOR_BYTES;
}


// Copys the data from inTrack into outSector so that it is aligned to byte properly
void extractRawSector(const unsigned char* inTrack, const uint32_t dataLengthInBits, const uint32_t bitPos, RawEncodedSector& outSector) {
	unsigned char byteOut = 0;
	unsigned int byteOutPosition = 0;

	uint32_t realBitPos = bitPos;
		
	unsigned char* memOut = outSector;

	// This is mis-aligned.  So we need to shift the data into byte boundarys
	for (uint32_t byteOutPos=0; byteOutPos < RAW_SECTOR_SIZE; byteOutPos++) {
		for (uint32_t bit=0; bit<=7; bit++) {
			*memOut <<= 1;
			const uint32_t trackBytePos = realBitPos >> 3;
			const uint32_t trackBitPos = 7 - (realBitPos & 7);

			if (inTrack[trackBytePos] & (1 << trackBitPos)) *memOut |= 1;
			realBitPos = (realBitPos + 1) % dataLengthInBits;
		}

		// Move along and reset
		memOut++;
	}
}

// MFM decoding algorithm
// *input;	MFM coded data buffer (size == 2*data_size) 
// *output;	decoded data buffer (size == data_size) 
// Returns the checksum calculated over the data
uint32_t decodeMFMdata(const uint32_t* input, uint32_t* output, const unsigned int data_size) {
	uint32_t odd_bits, even_bits;
	uint32_t chksum = 0L;
	unsigned int count;

	// the decoding is made here long by long : with data_size/4 iterations 
	for (count = 0; count < data_size / 4; count++) {
		odd_bits = *input;					// longs with odd bits 
		even_bits = *(uint32_t*)(((unsigned char*)input) + data_size);   // longs with even bits - located 'data_size' bytes after the odd bits

		chksum ^= odd_bits;              // XOR Checksum
		chksum ^= even_bits;

		*output = ((even_bits & MFM_MASK) | ((odd_bits & MFM_MASK) << 1));
		input++;      /* next 'odd' long and 'even bits' long  */
		output++;     /* next location of the future decoded long */
	}
	return chksum & MFM_MASK;
}

// Decode the sector.  Returns the number of checksum/errors found
void decodeSector(const RawEncodedSector& rawSector, const uint32_t trackNumber, const uint32_t expectedNumSectors, DecodedTrack& decodedTrack) {
	DecodedSector sector;
	SectorHeader header;
	sector.numErrors = 0;

	// Easier to operate on
	const unsigned char* sectorData = rawSector;

	// Read the first 4 bytes (8).  This  is the track header data	
	uint32_t headerChecksumCalculated = decodeMFMdata((uint32_t*)(sectorData), (uint32_t*)&header, 4);
	// Decode the label data and update the checksum
	uint32_t sectorLabel[4];
	headerChecksumCalculated ^= decodeMFMdata((uint32_t*)(sectorData + 8), (uint32_t*)&sectorLabel[0], 16);
	// Get the checksum for the header
	uint32_t headerChecksum;
	decodeMFMdata((uint32_t*)(sectorData + 40), (uint32_t*)&headerChecksum, 4);  // (computed on mfm longs, longs between offsets 8 and 44 == 2 * (1 + 4) longs)
	
	// If the header checksum fails we just cant trust anything we received
	if (headerChecksum != headerChecksumCalculated) sector.numErrors+= 10;

	// Check if the header contains valid fields
	if (header.trackFormat != 0xFF) return;  // this also blocks IBM sectors from being detected incorrectly
	// Can't use this sector anyway
	if (header.sectorNumber > (expectedNumSectors - 1)) return;
	if (header.trackNumber > 166) sector.numErrors++;
	if (header.sectorsRemaining > expectedNumSectors) sector.numErrors++;
	if (header.sectorsRemaining < 1) sector.numErrors++;

	// And is it from the track we expected?
	if (header.trackNumber != trackNumber) sector.numErrors++;

	// Get the checksum for the data
	uint32_t dataChecksum;
	decodeMFMdata((uint32_t*)(sectorData + 48), (uint32_t*)&dataChecksum, 4);

	// Decode the data and receive it's checksum
	sector.data.resize(SECTOR_BYTES);
	uint32_t dataChecksumCalculated = decodeMFMdata((uint32_t*)(sectorData + 56), (uint32_t*)&sector.data[0], SECTOR_BYTES); // (from 64 to 1088 == 2*512 bytes)

	if (dataChecksum != dataChecksumCalculated) 
		sector.numErrors++;

	// Store the one with the least errors if there's duplicates
	auto it = decodedTrack.sectors.find(header.sectorNumber);

	if (it == decodedTrack.sectors.end())
		decodedTrack.sectors.insert(std::make_pair(header.sectorNumber, sector));
	else {
		// See which one has less errors and overwrite if needed
		if (sector.numErrors < it->second.numErrors)
			it->second = sector;
	}
}

// Search for sectors in the data supplied
void findSectors_AMIGA(const uint8_t* track, const uint32_t dataLengthInBits, const bool isHD, const uint32_t trackNumber, const uint32_t expectedNumSectors, DecodedTrack& decodedTrack) {
	// Work out what we need to search for which is syncsync
	const uint32_t search = (AMIGA_WORD_SYNC | (((uint32_t)AMIGA_WORD_SYNC) << 16));

	// Prepare our test buffer
	uint32_t decoded = 0;

	// Search with an overlap of approx 3 raw sectors worth of data
	const uint32_t totalBitsToSearch = dataLengthInBits + (RAW_SECTOR_SIZE * 8 * 3);
	const uint32_t expectedSectors = expectedNumSectors ? expectedNumSectors : (isHD ? NUM_SECTORS_PER_TRACK_HD : NUM_SECTORS_PER_TRACK_DD);

	int nextTrackBitCount = 0;

	RawEncodedSector alignedSector;

	// run the entire track length with some space to wrap around
	for (uint32_t bit = 0; bit< totalBitsToSearch; bit++) {
		const uint32_t realBitPos = bit % dataLengthInBits;
		const uint32_t trackBytePos = realBitPos >> 3;
		const uint32_t trackBitPos =  7- (realBitPos & 7);
		decoded <<= 1;   // shift off one bit to make room for the new bit
		if (track[trackBytePos] & (1 << trackBitPos)) decoded |= 1;

		if (decoded == search) {
			// Extract the sector and skip past the data
			extractRawSector(track, dataLengthInBits, (bit + 1) % dataLengthInBits, alignedSector);

			// Now see if there's a valid sector there.  We now only skip the sector if its valid, incase rogue data gets in there
			decodeSector(alignedSector, trackNumber, expectedSectors, decodedTrack);			
		}
	}

	// Fill in the missing ones
	decodedTrack.sectorsWithErrors = 0;
	for (uint32_t sec = 0; sec < expectedSectors; sec++) {
		auto it = decodedTrack.sectors.find(sec);

		// Does a sector with this number exist?
		if (it == decodedTrack.sectors.end()) {
			if (expectedNumSectors) {
				// No. Create a dummy one - VERY NOT IDEAL!
				DecodedSector tmp;
				tmp.data.resize(SECTOR_BYTES);
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

// MFM encoding algorithm - this just writes the actual data bits in the right places
// *input;	RAW data buffer (size == data_size) 
// *output;	MFM encoded buffer (size == data_size*2) 
// Returns the checksum calculated over the data
uint32_t encodeMFMdata(const uint32_t* input, uint32_t* output, const unsigned int data_size) {
	uint32_t chksum = 0L;
	unsigned int count;

	uint32_t* outputOdd = output;
	uint32_t* outputEven = (uint32_t*)(((unsigned char*)output) + data_size);

	// Encode over two passes.  First split out the odd and even data, then encode the MFM values, the /4 is because we're working in longs, not bytes
	for (count = 0; count < data_size / 4; count++) {
		*outputEven = *input & MFM_MASK;
		*outputOdd = ((*input) >> 1) & MFM_MASK;
		outputEven++;
		outputOdd++;
		input++;
	}

	// Checksum calculator
	// Encode over two passes.  First split out the odd and even data, then encode the MFM values, the /4 is because we're working in longs, not bytes
	for (count = 0; count < (data_size / 4) * 2; count++) {
		chksum ^= *output;
		output++;
	}

	return chksum & MFM_MASK;
}

// Encode a sector into the correct format for disk
void encodeSector(const uint32_t trackNumber, const uint32_t sectorNumber, const uint32_t totalSectors, const RawDecodedSector& input, RawEncodedSector& encodedSector, unsigned char& lastByte) {
	// Sector Start
	encodedSector[0] = (lastByte & 1) ? 0x2A : 0xAA;
	encodedSector[1] = 0xAA;
	encodedSector[2] = 0xAA;
	encodedSector[3] = 0xAA;
	// Sector Sync
	encodedSector[4] = 0x44;
	encodedSector[5] = 0x89;
	encodedSector[6] = 0x44;
	encodedSector[7] = 0x89;

	// MFM Encoded header
	SectorHeader header;
	memset(&header, 0, sizeof(header));

	header.trackFormat = 0xFF;
	header.trackNumber = trackNumber;
	header.sectorNumber = sectorNumber;
	header.sectorsRemaining = totalSectors - sectorNumber;  //1..11

	// Shouldnt happen but important
	if (input.size() != SECTOR_BYTES) return;

	uint32_t sectorLabel[4] = { 0,0,0,0 };
	uint32_t headerChecksumCalculated = encodeMFMdata((const uint32_t*)&header, (uint32_t*)&encodedSector[8], 4);
	// Then theres the 16 bytes of the volume label that isnt used anyway
	headerChecksumCalculated ^= encodeMFMdata((const uint32_t*)&sectorLabel, (uint32_t*)&encodedSector[16], 16);
	// Thats 40 bytes written as everything doubles (8+4+4+16+16). - Encode the header checksum
	encodeMFMdata((const uint32_t*)&headerChecksumCalculated, (uint32_t*)&encodedSector[48], 4);
	// And move on to the data section.  Next should be the checksum, but we cant encode that until we actually know its value!
	uint32_t dataChecksumCalculated = encodeMFMdata((const uint32_t*)&input[0], (uint32_t*)&encodedSector[64], SECTOR_BYTES);
	// And add the checksum
	encodeMFMdata((const uint32_t*)&dataChecksumCalculated, (uint32_t*)&encodedSector[56], 4);

	// Now fill in the MFM clock bits
	bool lastBit = encodedSector[7] & (1 << 0);
	bool thisBit = lastBit;

	// Clock bits are bits 7, 5, 3 and 1
	// Data is 6, 4, 2, 0
	for (int count = 8; count < RAW_SECTOR_SIZE; count++) {
		for (int bit = 7; bit >= 1; bit -= 2) {
			lastBit = thisBit;
			thisBit = encodedSector[count] & (1 << (bit - 1));

			if (!(lastBit || thisBit)) {
				// Encode a 1!
				encodedSector[count] |= (1 << bit);
			}
		}
	}

	lastByte = encodedSector[RAW_SECTOR_SIZE - 1];
}

// Encodes all sectors into the buffer provided and returns the number of bytes that need to be written to disk 
// mfmBufferSizeBytes needs to be at least 13542 or DD and 27076 for HD                                
uint32_t encodeSectorsIntoMFM_AMIGA(const bool isHD, const DecodedTrack& decodedTrack, const uint32_t trackNumber, const uint32_t mfmBufferSizeBytes, void* memBuffer) {
	// Filler padding added to the front of the data to wipe old data nd to provide a very stable clock for the data
	const uint32_t fillerSize = PRE_FILLER + (isHD ? PRE_FILLER : 0);

	// Calculate total bytes we want to write - the extra 8 bytes is for post padding to clean up clock bits
	const uint32_t bytesRequired = (uint32_t)((sizeof(RawEncodedSector) * decodedTrack.sectors.size()) + fillerSize + 8);

	// Not enough space?
	if (mfmBufferSizeBytes < bytesRequired) return 0;

	unsigned char* output = (unsigned char*)memBuffer;
	unsigned char lastByte = 0xAA;
	memset(output, lastByte, fillerSize);
	output += fillerSize;

	// The order of the sectors does not matter
	for (const auto& sec : decodedTrack.sectors) {
		RawEncodedSector* out = (RawEncodedSector*)output;			
		encodeSector(trackNumber, (uint32_t)sec.first, (uint32_t)decodedTrack.sectors.size(), sec.second.data, *out, lastByte);
		output += sizeof(RawEncodedSector);
	}

	// Populate the remaining 8 bytes
	memset(output, 0xAA, 8);
	// Clock byte needs to be adjusted here if the last bit is set
	if (lastByte & 1) *output = 0x2F;

	return bytesRequired;
}