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

#include "pll.h"

#define CLOCK_CENTRE  2000   /* 2000ns = 2us */
#define CLOCK_MAX_ADJ 10     /* +/- 10% adjustment */
#define CLOCK_MIN ((CLOCK_CENTRE * (100 - CLOCK_MAX_ADJ)) / 100)
#define CLOCK_MAX ((CLOCK_CENTRE * (100 + CLOCK_MAX_ADJ)) / 100)

// Constructor
PLL::PLL() {
    reset();
    newTrack();
}

// Reset the PLL
void PLL::reset() {
    m_clock = CLOCK_CENTRE;
    m_nFluxSoFar = 0;

    m_bitsRemaining = 0;
    m_mfmData.clear();
}

// Reset the track buffer
void PLL::newTrack() {
    m_bitsRemaining = 0;
    m_mfmData.clear();
}

// Add the bit
void PLL::addBit(const uint8_t& bit) {
    if (m_bitsRemaining == 0) {
        m_mfmData.push_back(bit);
        m_bitsRemaining = 7;
    }
    else {
        m_bitsRemaining--;
        m_mfmData.back() <<= 1;
        m_mfmData.back() |= bit;
    }    
}

// Submit flux to the PLL
void PLL::decodeFlux(const uint32_t fluxTime) {
    m_nFluxSoFar += fluxTime;
    if (m_nFluxSoFar < (m_clock / 2)) return;

    // Work out how many zeros, and remaining flux
    int32_t clockedZeros = (m_nFluxSoFar - (m_clock / 2)) / m_clock;
    m_nFluxSoFar -= ((clockedZeros + 1) * m_clock);

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
    m_nFluxSoFar /= 2;

    // Convert flux time into MFM
    while (clockedZeros) {
        addBit(0);
        clockedZeros--;
    }
    addBit(1);
}

// Finishes the track, returns its size in BITS and a pointer to the buffer containing it which you should copy
uint32_t PLL::finaliseTrack(void** buffer) {
    if (!buffer) return 0;

    // First we need to shift the remaining bits in the last byte to be the most significiant
    if (m_bitsRemaining) m_mfmData.back() <<= m_bitsRemaining;
    
    *buffer = (void*)m_mfmData.data();
    return ((uint32_t)m_mfmData.size() * 8) - m_bitsRemaining;
}
