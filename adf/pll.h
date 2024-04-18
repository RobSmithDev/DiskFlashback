#pragma once

#include <stdint.h>
#include <vector>

class PLL {
private:
	// Clock
	int32_t m_clock = 0;
	int32_t m_latency = 0;
	int32_t m_prevLatency = 0;
	int32_t m_totalRealFlux = 0;
	int32_t m_nFluxSoFar = 0;
	uint8_t* buffer = nullptr;
	uint32_t bufferLength = 0;   // bits remaining
	uint32_t bufferPositionBYTE = 0;
	uint32_t bufferPositionBIT = 0;
	uint32_t bufferPosition = 0; // in bits

	// Add data to the output. Returns TRUE if full
	bool writeData(uint32_t numZeros);
	bool addBit(bool bit);
public:
	PLL();

	// Reset the track with the size of the buffer
	void newTrack(void* data, uint32_t maxLength);

	// Submit flux to the PLL - returns how much space is left IN BITS
	uint32_t decodeFlux(const std::vector<uint16_t>& flux);

	// Reset the PLL
	void reset();
};
