
#include "readwrite_file.h"
#include "ibm_sectors.h"
#include "amiga_sectors.h"

// Attempts to guess the number of sectors per track based on the supplied image size
uint32_t SectorRW_File::GuessSectorsPerTrackFromImageSize(const uint32_t imageSize, const uint32_t sectorSize) {
    const uint32_t fs = imageSize / sectorSize;

    switch (fs) {
        // IBM DD Sector
    case 80 * 2 * 9:
    case 81 * 2 * 9:
    case 82 * 2 * 9:
    case 83 * 2 * 9: return 9; 
        // Atari DD 10 Sector 
    case 80 * 2 * 10:
    case 81 * 2 * 10:
    case 82 * 2 * 10:
    case 83 * 2 * 10: return 10;
        // Atari DD 11 Sector / Amiga DD Sector
    case 80 * 2 * 11:
    case 81 * 2 * 11:
    case 82 * 2 * 11:
    case 83 * 2 * 11: return 11;
        // IBM HD Sector
    case 80 * 2 * 18:
    case 81 * 2 * 18:
    case 82 * 2 * 18:
    case 83 * 2 * 18: return 18;
        // Amiga HD Sector
    case 2 * 80 * 2 * 11:
    case 2 * 81 * 2 * 11:
    case 2 * 82 * 2 * 11:
    case 2 * 83 * 2 * 11: return 22;
    default:
        if (fs > 84 * 2 * 11) {
            // HD
            return 22;
        }
        else {
            // DD
            return 11;
        }
    }
}


SectorRW_File::SectorRW_File(const std::wstring& filename, HANDLE fle) : SectorCacheEngine(512 * 84 * 2 * 2 * 11), m_file(fle) {
    m_fileType = SectorType::stAmiga;  // default to Amiga

    m_serialNumber = 0x41444630;    // Default AMIGA serial number (ADF0)
    m_bytesPerSector = 512;
    m_sectorsPerTrack = 0;

    // See what type of file it is
    size_t i = filename.rfind(L".");
    if (i != std::wstring::npos) {
        std::wstring ext = filename.substr(i+1);
        for (WCHAR& c : ext) c = towupper(c);

        // See what type of file it is
        if ((ext == L"IMG") || (ext == L"IMA")) {
            m_fileType = SectorType::stIBM;
            m_serialNumber = 0x494D4130;
        } else
        if (ext == L"ST") {
            m_fileType = SectorType::stAtari;
            m_serialNumber = 0x53544630;
        }

        if (SUCCEEDED(SetFilePointer(m_file, 0, NULL, FILE_BEGIN))) {
            DWORD read;
            uint8_t data[128];
            if (ReadFile(m_file, data, 128, &read, NULL)) {
                if (read == 128) {
                    uint32_t totalSectors;
                    if (!getTrackDetails_IBM(data, m_serialNumber, totalSectors, m_sectorsPerTrack, m_bytesPerSector)) {
                        m_bytesPerSector = 512;
                        m_serialNumber = 0x41444630;
                    }
                    else {
                        m_totalTracks = max(80, (totalSectors / m_sectorsPerTrack) / 2);
                    }
                }
            }
        }
    }
    if (!m_sectorsPerTrack) m_sectorsPerTrack = SectorRW_File::GuessSectorsPerTrackFromImageSize(GetFileSize(fle, NULL));
    m_totalTracks = m_sectorsPerTrack ? 0 : (GetFileSize(fle, NULL) / m_sectorsPerTrack);
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

