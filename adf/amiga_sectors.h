#pragma once

// The code in these files was taken from ADFWriter which is part of the DrawBridge - aka ArduinoFloppyReader (and writer) code
// ... also by me!
#include <dokan/dokan.h>
#include <stdint.h>
#include <unordered_map>
#include "sectorCommon.h"

// Grabs a copy of the bootblock for the system required.  target must be 1024 bytes in size
void fetchBootBlockCode_AMIGA(bool ffs, uint8_t* target, const std::string& comment);

// Very simple 
void getTrackDetails_AMIGA(const bool isID, uint32_t& sectorsPerTrack, uint32_t& bytesPerSector);

// Searches for sectors - you can re-call this and it will update decodedTrack rather than replace it
void findSectors_AMIGA(const uint8_t* track, const uint32_t dataLengthInBits, const bool isHD, const uint32_t trackNumber, const uint32_t expectedNumSectors, DecodedTrack& decodedTrack);

// Encodes all sectors into the buffer provided and returns the number of bytes that need to be written to disk 
// mfmBufferSizeBytes needs to be at least 13542 or DD and 27076 for HD
uint32_t encodeSectorsIntoMFM_AMIGA(const bool isHD, const DecodedTrack& decodedTrack, const uint32_t trackNumber, const uint32_t mfmBufferSizeBytes, void* memBuffer);
