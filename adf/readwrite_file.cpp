
#include "readwrite_file.h"

SectorRW_File::SectorRW_File(const uint32_t maxCacheMem, HANDLE fle) : SectorCacheEngine(maxCacheMem), m_file(fle) {
    m_sectorsPerTrack = (GetFileSize(fle, NULL) < 89 * 2 * 11 * 512) ? 11 : 22;  // not actually used
}

SectorRW_File::~SectorRW_File() {
    CloseHandle(m_file);
}

// Fetch the size of the disk file
uint32_t SectorRW_File::getDiskDataSize() {
    return GetFileSize(m_file, NULL);
};

bool SectorRW_File::isDiskPresent() {
    return available();
}

bool SectorRW_File::isDiskWriteProtected() {
    return false;
}


bool SectorRW_File::available() {
    return m_file != INVALID_HANDLE_VALUE;
}

bool SectorRW_File::internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    if (SetFilePointer(m_file, sectorNumber * sectorSize, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        return false;

    DWORD read = 0;
    if (!ReadFile(m_file, data, sectorSize, &read, NULL))
        return false;
    
    if (read != sectorSize) return false;
    return true;
}

bool SectorRW_File::internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
    if (SetFilePointer(m_file, sectorNumber * sectorSize, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        return false;

    DWORD write = 0;
    if (!WriteFile(m_file, data, sectorSize, &write, NULL))
        return false;

    if (write != sectorSize) return false;

    return true;
}

