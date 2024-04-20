
#include "readwrite_file.h"
#include "ibm_sectors.h"
#include "amiga_sectors.h"

#define BYTESWAP(x)   (((x<<8) | (x>>8)) & 0xFFFF)

struct alignas(2) ATARTST_MSA_Header {
    uint16_t        idMarker;
    uint16_t        sectorsPerTrack;
    uint16_t        numHeads;
    uint16_t        firstTrack;
    uint16_t        lastTrack;
};


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
    m_firstTrack = 0;
    m_serialNumber = 0x41444630;    // Default AMIGA serial number (ADF0)
    m_bytesPerSector = 512;
    m_sectorsPerTrack = 0;
    m_numHeads = 2;
    m_mode = SectorMode::smNormal;
    m_totalTracks = 0;
    SetFilePointer(m_file, 0, NULL, FILE_BEGIN);

    // See what type of file it is
    size_t i = filename.rfind(L".");
    if (i != std::wstring::npos) {
        std::wstring ext = filename.substr(i + 1);
        for (WCHAR& c : ext) c = towupper(c);

        // See what type of file it is
        if ((ext == L"IMG") || (ext == L"IMA")) {
            m_fileType = SectorType::stIBM;
            m_serialNumber = 0x494D4130;
        }
        else
        if (ext == L"ST") {
            m_fileType = SectorType::stAtari;
            m_serialNumber = 0x53544630;
        }
        else
        if (ext == L"MSA") {
            ATARTST_MSA_Header header;
            DWORD read;
            if (!ReadFile(m_file, &header, sizeof(header), &read, NULL)) read = 0;
            if ((read != sizeof(header)) || (header.idMarker != 0x0F0E)) {
                CloseHandle(m_file);
                m_file = INVALID_HANDLE_VALUE;
                return;
            }
            m_firstTrack = BYTESWAP(header.firstTrack);
            m_totalTracks = (BYTESWAP(header.lastTrack) - m_firstTrack) + 1;
            m_numHeads = BYTESWAP(header.numHeads) + 1;
            m_totalTracks *= m_numHeads;
            m_sectorsPerTrack = BYTESWAP(header.sectorsPerTrack);
            m_fileType = SectorType::stAtari;
            m_serialNumber = 0x4D534120;
            m_mode = SectorMode::smMSA;
        }

        if ((m_fileType == SectorType::stIBM) || (m_fileType == SectorType::stAtari)) {
            uint8_t data[128];
            if (internalReadData(0, min(m_bytesPerSector, 128), data)) {
                uint32_t totalSectors;
                if (!getTrackDetails_IBM(data, m_serialNumber, m_numHeads, totalSectors, m_sectorsPerTrack, m_bytesPerSector)) {
                    m_bytesPerSector = 512;
                    m_numHeads = 2;
                    m_serialNumber = 0x41444630;
                }
                else m_totalTracks = totalSectors / m_sectorsPerTrack;
            }
        }
    }

    if (!m_sectorsPerTrack) m_sectorsPerTrack = SectorRW_File::GuessSectorsPerTrackFromImageSize(GetFileSize(fle, NULL));
    if (!m_totalTracks) m_totalTracks = m_sectorsPerTrack ? (GetFileSize(fle, NULL) / m_sectorsPerTrack) / m_bytesPerSector : 80;
}

// Decode the track from this point in the file
bool SectorRW_File::decodeMSATrack(DecodedTrack& track) {
    const uint32_t uncompressedTrackSize = m_bytesPerSector * m_sectorsPerTrack;
    DWORD read;

    // Read in the data
    if (track.dataSize == uncompressedTrackSize) {
        // Read straight in, its not compressed
        track.data.resize(uncompressedTrackSize);
        if (!ReadFile(m_file, &track.data[0], track.dataSize, &read, NULL)) read = 0;
        return read == track.dataSize;
    }
    else {
        // Read in, and then decompress it
        std::vector<uint8_t> temp;
        temp.resize(track.dataSize);
        if (!ReadFile(m_file, (LPVOID)&temp[0], track.dataSize, &read, NULL)) read = 0;
        if (read != track.dataSize) return false;

        // Decode the data
        for (size_t pos = 0; pos < temp.size(); pos++) {
            if (temp[pos] == 0xE5) {
                // Decompress - basic RLE
                uint16_t numBytes = (temp[pos + 2] << 8) | temp[pos + 3];
                size_t currentSize = track.data.size();
                track.data.resize(currentSize + numBytes);
                memset(&track.data[currentSize], temp[pos + 1], numBytes);
                pos += 3;
            }
            else track.data.push_back(temp[pos]);
        }

        return track.data.size() >= uncompressedTrackSize;
    }


    return false;
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
    return m_mode == SectorMode::smMSA;
}


bool SectorRW_File::available() {
    return m_file != INVALID_HANDLE_VALUE;
}

bool SectorRW_File::internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    DWORD read = 0;

    switch (m_mode) {
    case SectorMode::smNormal:
        if (SetFilePointer(m_file, sectorNumber * sectorSize, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return false;
        if (!ReadFile(m_file, data, sectorSize, &read, NULL)) return false;
        return (read == sectorSize);

    case SectorMode::smMSA: 
    {
        uint32_t trackSeek = sectorNumber / m_sectorsPerTrack;
        auto i = m_trackSearch.find(trackSeek);
        if (i == m_trackSearch.end()) {
            // Search for it. 
            uint32_t startSeekPos = sizeof(ATARTST_MSA_Header);
            uint32_t track = m_firstTrack;

            if (m_trackSearch.size()) {
                // Kickstart the search point rather than start from the first track we'll skip past the highest found so far
                auto trk = --m_trackSearch.end();
                track = trk->first + 1;
                startSeekPos = trk->second.seekPos + trk->second.dataSize;
            }
            if (SetFilePointer(m_file, startSeekPos, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return false;

            while (track <= trackSeek) {
                DecodedTrack newTrack;
                DWORD read;
                if (!ReadFile(m_file, &newTrack.dataSize, sizeof(newTrack.dataSize), &read, NULL)) read = 0;
                if (read != sizeof(newTrack.dataSize)) return false;
                newTrack.dataSize = BYTESWAP(newTrack.dataSize);  // byte swap
                startSeekPos += sizeof(newTrack.dataSize);
                newTrack.seekPos = startSeekPos;
                if (!decodeMSATrack(newTrack)) return false;

                // Save the data
                m_trackSearch.insert(std::pair(track, newTrack));
                track++;
            }
            i = m_trackSearch.find(trackSeek);
            if (i == m_trackSearch.end()) return false;
        }
        // If we get here then the track exists and we can just pull the data out
        uint32_t memPos = (sectorNumber % m_sectorsPerTrack) * sectorSize;
        if (memPos + sectorSize > i->second.data.size()) return false;
        memcpy_s(data, sectorSize, &i->second.data[memPos], sectorSize);
        return true;
    }
    break;

    default: return false;
    }
}

bool SectorRW_File::internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
    DWORD write = 0;

    switch (m_mode) {
    case SectorMode::smNormal:
        if (SetFilePointer(m_file, sectorNumber * sectorSize, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return false;
        if (!WriteFile(m_file, data, sectorSize, &write, NULL)) return false;
        return write == sectorSize;

    default:
        return false;
    }
}

