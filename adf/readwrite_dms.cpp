
#include "readwrite_dms.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HEADLEN 56
#define THLEN 20
#define TRACK_BUFFER_LEN 32000
#define TEMP_BUFFER_LEN 32000

#define SECTORSIZE 512

extern "C" {
#include "xdms/src/cdata.h"
#include "xdms/src/u_init.h"
#include "xdms/src/u_rle.h"
#include "xdms/src/u_quick.h"
#include "xdms/src/u_medium.h"
#include "xdms/src/u_deep.h"
#include "xdms/src/u_heavy.h"
#include "xdms/src/crc_csum.h"
}

// This uses code from the "pfile" files in a more suitable form

/*  DMS uses a lame encryption  */
/*static void dms_decrypt(UCHAR* p, USHORT len) {
    USHORT t;

    while (len--) {
        t = (USHORT)*p;
        *p++ ^= (UCHAR)PWDCRC;
        PWDCRC = (USHORT)((PWDCRC >> 1) + t);
    }
}
*/

// We force the cache size so the entire DMS file fits into memory
SectorRW_DMS::SectorRW_DMS(HANDLE fle) : SectorCacheEngine(512 * 84 * 2 * 2 * 11) {
    
    // Buffers needs internally by DMS
    uint8_t* b1 = (uint8_t*)malloc((size_t)TRACK_BUFFER_LEN);
    uint8_t* b2 = (uint8_t*)malloc((size_t)TRACK_BUFFER_LEN);
    uint8_t* text = (uint8_t*)malloc((size_t)TEMP_BUFFER_LEN);

    if (b1 && b2 && text) {
        m_validFile = parseDMSHeader(fle, b1, b2, text);
        if (m_validFile) unpackCylinders(fle, b1, b2, text);
    }

    if (b1) free(b1);
    if (b2) free(b2);
    if (text) free(text);
    CloseHandle(fle);
}

// Unpack the track with random (ish) access to the track 
// DMS sometimes needs to be extracted in order!?
bool SectorRW_DMS::unpackCylinders(HANDLE fle, uint8_t* b1, uint8_t* b2, uint8_t* text) {
    DWORD read;    

    // Run until we run out of data
    for (size_t c = 0; c < 80; c++) {
        if (!ReadFile(fle, b1, THLEN, &read, NULL)) return false;
        if (read != THLEN) return false;

        if ((b1[0] != 'T') || (b1[1] != 'R')) return false;
        USHORT hcrc = (USHORT)((b1[THLEN - 2] << 8) | b1[THLEN - 1]);
        if (CreateCRC(b1, (ULONG)(THLEN - 2)) != hcrc) return false;
        USHORT pklen1 = (USHORT)((b1[6] << 8) | b1[7]);	    /*  Length of packed track data as in archive  */
        USHORT pklen2 = (USHORT)((b1[8] << 8) | b1[9]);	    /*  Length of data after first unpacking  */
        USHORT number = (USHORT)((b1[2] << 8) | b1[3]);     /*  Number of cylinder  */
        USHORT unpklen = (USHORT)((b1[10] << 8) | b1[11]);	/*  Length of data after subsequent rle unpacking */
        USHORT dcrc = (USHORT)((b1[16] << 8) | b1[17]);	    /*  Track Data CRC BEFORE unpacking  */
        USHORT flags = b1[12];		                        /*  control flags  */
        USHORT cmode = b1[13];		                        /*  compression mode used  */
        USHORT usum = (USHORT)((b1[14] << 8) | b1[15]);	/*  Track Data CheckSum AFTER unpacking  */

        if ((pklen1 > TRACK_BUFFER_LEN) || (pklen2 > TRACK_BUFFER_LEN) || (unpklen > TRACK_BUFFER_LEN)) return false;
        
        // Skip fake boot block advert
        if (((number == 0) && (unpklen == 1024)) || (number >= 80)) {
           //
        }
        else {
            // Read the buffer
            if (!ReadFile(fle, b1, pklen1, &read, NULL)) return false;
            if (read != pklen1) return false;

            // Check track CRC
            if (CreateCRC(b1, (ULONG)pklen1) == dcrc) {
                // dms_decrypt(b1,pklen1);

                // Try to unpack it
                if (unpklen > 2048) {
                    memset(b2, 0, unpklen);
                    if (decompressTrack(b1, b2, text, pklen2, unpklen, cmode, flags)) {
                        if (usum != Calc_CheckSum(b2, (ULONG)unpklen)) {
                            OutputDebugStringA("CHGECKFAIL");
                        }

                        // Save sectors into cache
                        uint32_t sectorStart = number * 2 * m_sectorsPerTrack;
                        char* src = (char*)b2;
                        int32_t remaining = (int32_t)unpklen;
                        while (remaining >= SECTORSIZE) {
                            writeCache(sectorStart++, SECTORSIZE, src);
                            src += SECTORSIZE;
                            remaining -= SECTORSIZE;
                        }
                    }
                }
            }
        }
    } 
    return true;
}

// Decompress the track!
bool SectorRW_DMS::decompressTrack(uint8_t* b1, uint8_t* b2, uint8_t* text, uint16_t pklen2, uint16_t unpklen, uint16_t cmode, uint16_t flags) {
    switch (cmode) {
    case 0:
        /*   No Compression   */
        memcpy(b2, b1, (size_t)unpklen);
        break;
    case 1:
        /*   Simple Compression   */
        if (Unpack_RLE((UCHAR*)b1, (UCHAR*)b2, unpklen)) return false;
        break;
    case 2:
        /*   Quick Compression   */
        if (Unpack_QUICK(b1, b2, pklen2, text)) return false;
        if (Unpack_RLE(b2, b1, unpklen)) return false;
        memcpy(b2, b1, (size_t)unpklen);
        break;
    case 3:
        /*   Medium Compression   */
        if (Unpack_MEDIUM(b1, b2, pklen2, text)) return false;
        if (Unpack_RLE(b2, b1, unpklen)) return false;
        memcpy(b2, b1, (size_t)unpklen);
        break;
    case 4:
        /*   Deep Compression   */
        if (Unpack_DEEP(b1, b2, pklen2, text)) return false;
        if (Unpack_RLE(b2, b1, unpklen)) return false;
        memcpy(b2, b1, (size_t)unpklen);
        break;
    case 5:
    case 6:
        /*   Heavy Compression   */
        if (cmode == 5) {
            /*   Heavy 1   */
            if (Unpack_HEAVY(b1, b2, flags & 7, pklen2, text)) return false;
        }
        else {
            /*   Heavy 2   */
            if (Unpack_HEAVY(b1, b2, flags | 8, pklen2, text)) return false;
        }
        if (flags & 4) {
            /*  Unpack with RLE only if this flag is set  */
            if (Unpack_RLE(b2, b1, unpklen)) return false;
            memcpy(b2, b1, (size_t)unpklen);
        }
        break;
    default:
        return false;
    }

    if (!(flags & 1)) Init_Decrunchers((UCHAR*)text);

    return true;
}

// Parse the DMS
bool SectorRW_DMS::parseDMSHeader(HANDLE fle, uint8_t* b1, uint8_t* b2, uint8_t* text) {
    m_sectorsPerTrack = (GetFileSize(fle, NULL) < 89 * 2 * 11 * SECTORSIZE) ? 11 : 22;

    // Read header and validate
    DWORD read;
    if (!ReadFile(fle, b1, HEADLEN, &read, NULL)) return false;
    if ((b1[0] != 'D') || (b1[1] != 'M') || (b1[2] != 'S') || (b1[3] != '!')) return false;
    USHORT hcrc = (USHORT)((b1[HEADLEN - 2] << 8) | b1[HEADLEN - 1]);
    if (hcrc != CreateCRC(b1 + 4, (ULONG)(HEADLEN - 6))) return false;

    m_geninfo = (USHORT)((b1[10] << 8) | b1[11]);
    m_diskSize = (ULONG)((((ULONG)b1[25]) << 16) | (((ULONG)b1[26]) << 8) | (ULONG)b1[27]);
    m_diskType = (USHORT)((b1[50] << 8) | b1[51]);

    // Not supported >6
    if (m_diskType > 6) return false;

    // Password required!?
    if (m_geninfo & 2) return false;

    Init_Decrunchers((UCHAR*)text);

    return true;
}

SectorRW_DMS::~SectorRW_DMS() {
}

// Fetch the size of the disk file
uint32_t SectorRW_DMS::getDiskDataSize() {
    return m_diskSize;
};

bool SectorRW_DMS::isDiskPresent() {
    return available();
}

bool SectorRW_DMS::available() {
    return m_validFile;
}

bool SectorRW_DMS::internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    return false;
}


