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
