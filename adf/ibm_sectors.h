#pragma once


#include <dokan/dokan.h>
#include <stdint.h>
#include <unordered_map>
#include "sectorCommon.h"

// Feed in Track 0, sector 0 and this will try to extract the number of sectors per track, or 0 on error
bool getTrackDetails_IBM(const uint8_t* sector, uint32_t& serialNumber, uint32_t& totalSectors, uint32_t& sectorsPerTrack, uint32_t& bytesPerSector);
bool getTrackDetails_IBM(const DecodedTrack* decodedTrack, uint32_t& serialNumber, uint32_t& totalSectors, uint32_t& sectorsPerTrack, uint32_t& bytesPerSector);

// Searches for sectors - you can re-call this and it will update decodedTrack rather than replace it
// nonstandardTimings is set to true if this uses non-standard timings like those used by Atari etc
void findSectors_IBM(const uint8_t* track, const uint32_t dataLengthInBits, const bool isHD, const uint32_t trackNumber, const uint32_t expectedNumSectors, DecodedTrack& decodedTrack, bool& nonstandardTimings);
void findSectors_IBM(const uint8_t* track, const uint32_t dataLengthInBits, const bool isHD, const uint32_t trackNumber, const uint32_t expectedNumSectors, DecodedTrack& decodedTrack);

// Encode the track supplied into a raw MFM bit-stream
uint32_t encodeSectorsIntoMFM_IBM(const bool isHD, const bool forceAtariTiming, DecodedTrack* decodedTrack, const uint32_t trackNumber, uint32_t mfmBufferSizeBytes, void* trackData);


