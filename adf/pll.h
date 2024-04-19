#pragma once

#include <stdint.h>
#include <vector>

class PLL {
private:
	// Clock
	int32_t m_clock = 0;
	int32_t m_nFluxSoFar = 0;

	// Decoded buffer 
	std::vector<uint8_t> m_mfmData;
	// How many bits remaining until we need a new byte
	uint32_t m_bitsRemaining = 0;

	inline void addBit(const uint8_t& bit);
public:
	PLL();

	// Reset the PLL
	void reset();

	// Reset the track buffer
	void newTrack();

	// Submit flux to the PLL
	void decodeFlux(const uint32_t fluxTime);

	// Finishes the track, returns its size in BITS and a pointer to the buffer containing it which you should copy
	uint32_t finaliseTrack(void** buffer);
};
