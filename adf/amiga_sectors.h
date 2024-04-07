#pragma once

// The code in these files was taken from ADFWriter which is part of the DrawBridge - aka ArduinoFloppyReader (and writer) code
// ... also by me!
#include <Windows.h>
#include <stdint.h>
#include <unordered_map>
#include "sectorCommon.h"

// These were borrowed from the WinUAE source code, HOWEVER, they're identical to what the Amiga OS writes with the INSTALL command
extern uint8_t bootblock_ofs[];
extern uint8_t bootblock_ffs[];

// Very simple 
void getTrackDetails_AMIGA(const bool isID, uint32_t& sectorsPerTrack, uint32_t& bytesPerSector);

// Searches for sectors - you can re-call this and it will update decodedTrack rather than replace it
void findSectors_AMIGA(const uint8_t* track, const uint32_t dataLengthInBits, const bool isHD, const uint32_t trackNumber, const uint32_t expectedNumSectors, DecodedTrack& decodedTrack);

// Encodes all sectors into the buffer provided and returns the number of bytes that need to be written to disk 
// mfmBufferSizeBytes needs to be at least 13542 or DD and 27076 for HD
uint32_t encodeSectorsIntoMFM_AMIGA(const bool isHD, const DecodedTrack& decodedTrack, const uint32_t trackNumber, const uint32_t mfmBufferSizeBytes, void* memBuffer);
