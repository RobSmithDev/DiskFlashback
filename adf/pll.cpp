#include "pll.h"

#define CLOCK_CENTRE  2000   /* 2000ns = 2us */
#define CLOCK_MAX_ADJ 10     /* +/- 10% adjustment */
#define CLOCK_MIN ((CLOCK_CENTRE * (100 - CLOCK_MAX_ADJ)) / 100)
#define CLOCK_MAX ((CLOCK_CENTRE * (100 + CLOCK_MAX_ADJ)) / 100)

// Constructor
PLL::PLL() {
    reset();
}

// Reset the PLL
void PLL::reset() {
    m_clock = CLOCK_CENTRE;
    m_latency = 0;
    m_totalRealFlux = 0;
    m_prevLatency = 0;
    m_nFluxSoFar = 0;
}

// Reset the track with the size of the buffer
void PLL::newTrack(void* data, uint32_t maxLength) {
    buffer = (uint8_t*) data;
    bufferLength = maxLength * 8;
    bufferPosition = 0;
    bufferPositionBYTE = 0;
    bufferPositionBIT = 0;
}

// Add the bit
bool PLL::addBit(bool bit) {
    if (bufferPosition >= bufferLength) return true;  // full
    bufferPosition++;

    buffer[bufferPositionBYTE] <<= 1;
    if (bit) buffer[bufferPositionBYTE] |= 1;

    bufferPositionBIT++;
    if (bufferPositionBIT >= 8) {
        bufferPositionBIT = 0;
        bufferPositionBYTE++;
    }
    return false;
}

// Submit flux to the PLL
uint32_t PLL::decodeFlux(const std::vector<uint16_t>& flux) {
    for (const uint16_t& fluxTime : flux) {
        m_nFluxSoFar += fluxTime;
        if (m_nFluxSoFar < (m_clock / 2)) continue;

        // Work out how many zeros, and remaining flux
        const int clockedZeros = (m_nFluxSoFar - (m_clock / 2)) / m_clock;
        m_nFluxSoFar -= ((clockedZeros + 1) * m_clock);
        m_latency += ((clockedZeros + 1) * m_clock);

        // PLL: Adjust clock frequency according to phase mismatch.
        if ((clockedZeros >= 1) && (clockedZeros <= 3)) {
            // In sync: adjust base clock by 10% of phase mismatch.
            m_clock += (m_nFluxSoFar / (int)(clockedZeros + 1)) / 10;
        }
        else {
            // Out of sync: adjust base clock towards centre.
            m_clock += (CLOCK_CENTRE - m_clock) / 10;
        }

        // Clamp the clock's adjustment range.
        m_clock = std::max(CLOCK_MIN, std::min(CLOCK_MAX, m_clock));

        // Authentic PLL: Do not snap the timing window to each flux transition.
        const uint32_t new_flux = m_nFluxSoFar / 2;
        m_latency += m_nFluxSoFar - new_flux;
        m_nFluxSoFar = new_flux;

        if (writeData(clockedZeros)) return 0;

        m_prevLatency = m_latency;
    }
    return bufferLength - bufferPosition;
}

// Add data to the output. Returns TRUE if full
bool PLL::writeData(uint32_t numZeros) {
    while (numZeros) {
        if (addBit(false)) return true;
        numZeros--;
    }
    return addBit(true);
}
