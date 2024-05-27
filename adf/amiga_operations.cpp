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

#include <dokan/dokan.h>
#include "amiga_operations.h"
#include <time.h>
#include <algorithm>
#include <Shlobj.h>

static const std::string CommonAmigaFileExtensions = ".DMS.PP.LHZ.LHA.LZX.IFF.GUIDE.ADF.8SVX.MOD.C.ASM.PP.";


DokanFileSystemAmiga::DokanFileSystemAmiga(DokanFileSystemManager* owner, bool autoRename) : DokanFileSystemBase(owner), m_autoRemapFileExtensions(autoRename) {
}
DokanFileSystemAmiga::ActiveFileIO::ActiveFileIO(DokanFileSystemAmiga* owner) : m_owner(owner) {};
// Add Move constructor
DokanFileSystemAmiga::ActiveFileIO::ActiveFileIO(DokanFileSystemAmiga::ActiveFileIO&& source) noexcept {
    this->m_owner = source.m_owner;
    source.m_owner = nullptr;
}
DokanFileSystemAmiga::ActiveFileIO::~ActiveFileIO() {
    if (m_owner) m_owner->clearFileIO();
}

// Sets a current file info block as active (or NULL for not) so DokanResetTimeout can be called if needed
void DokanFileSystemAmiga::setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo) {
    owner()->getBlockDevice()->setActiveFileIO(dokanfileinfo);
}

// Let the system know I/O is currently happenning.  ActiveFileIO must be kepyt in scope until io is complete
DokanFileSystemAmiga::ActiveFileIO DokanFileSystemAmiga::notifyIOInUse(PDOKAN_FILE_INFO dokanfileinfo) {
    setActiveFileIO(dokanfileinfo);
    return ActiveFileIO(this);
}


// Handle a note about the remap of file extension
void DokanFileSystemAmiga::handleRemap(const std::wstring& windowsPath, const std::string& amigaFilename, std::wstring& windowsFilename) {
    std::wstring path = windowsPath;
    auto i = path.find(L":\\");
    if (i != std::wstring::npos) path = path.substr(i + 2);
    i = path.find(L":");
    if (i != std::wstring::npos) path = path.substr(i + 1);

    auto f = m_specialRenameMap.find(path + windowsFilename);
    if (f == m_specialRenameMap.end()) 
        m_specialRenameMap.insert(std::pair(path + windowsFilename, amigaFilename));
}

// Auto rename change
void DokanFileSystemAmiga::changeAutoRename(bool autoRename) {
    if (m_autoRemapFileExtensions == autoRename) return;

    std::map<std::wstring, std::string> originalMap = m_specialRenameMap;

    m_autoRemapFileExtensions = autoRename;

    resetFileSystem();

    DOKAN_HANDLE handle = owner()->getDonakInstance();

    for (const auto& amigaFilename : originalMap) {
        // Get windows path
        std::wstring winName, winPath, newWinName;
        auto f = amigaFilename.first.rfind(L'\\');
        if (f != std::wstring::npos) {
            winPath = amigaFilename.first.substr(0, f);
            winName = amigaFilename.first.substr(f + 1);
        }
        else winName = amigaFilename.first;
        winPath = owner()->getMountPoint() + winPath;

        amigaFilenameToWindowsFilename(winPath, amigaFilename.second, newWinName);

        if (newWinName != winName) {
            newWinName = winPath + newWinName;
            winName = winPath + winName;
            SHChangeNotify(SHCNE_RENAMEITEM, SHCNF_PATH | SHCNF_FLUSHNOWAIT, winName.c_str(), newWinName.c_str());
        }
    }
}

void DokanFileSystemAmiga::resetFileSystem() {
    m_safeFilenameMap.clear();
    m_specialRenameMap.clear();
}


void DokanFileSystemAmiga::clearFileIO() {
    setActiveFileIO(nullptr);
}

// Makes sure the name supplied is actually a safe windows filename
void makePathFilenameComponentSafe(std::string& name) {
    // work forwards through name, replacing forbidden characters
    for (char& o : name)
        if (o < 32 || o == ':' || o == '*' || o == '"' || o == '?' || o == '<' || o == '>' || o == '|' || o == '/' || o == '\\')
            o = '_';

    // work backwards through name, replacing forbidden trailing chars 
    for (size_t i = name.length() - 1; i > 0; i--)
        if ((name[i] == '.') || (name[i] == ' '))
            name[i] = '_'; else break;

    // Check name without any extension
    const size_t pos = name.find('.');
    const size_t lenToCheck = (pos == std::string::npos) ? name.length() : pos;

    // is the name (excluding extension) a reserved name in Windows? 
    static const char* reserved_names[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
    for (unsigned int i = 0; i < sizeof(reserved_names) / sizeof(char**); i++)
        if ((lenToCheck >= strlen(reserved_names[i])) && (_strnicmp(reserved_names[i], name.c_str(), lenToCheck) == 0)) {
            name.insert(name.begin(), '_');
            break;
        }
}



// Handles fixing filenames so they're amiga compatable
void DokanFileSystemAmiga::amigaFilenameToWindowsFilename(const std::wstring& windowsPath, const std::string& amigaFilename, std::wstring& windowsFilename) {
    auto fle = m_safeFilenameMap.find(amigaFilename);
    if (fle != m_safeFilenameMap.end()) {
        windowsFilename = fle->second;
        if (m_autoRemapFileExtensions) handleRemap(windowsPath, amigaFilename, windowsFilename);
        return;
    }

    std::string name = amigaFilename;
    makePathFilenameComponentSafe(name);
   
    ansiToWide(name, windowsFilename);

    bool ret = false;
    // get where the file extension might be
    size_t fileExt1 = windowsFilename.find(L".");
    size_t fileExt2 = windowsFilename.rfind(L".");
    if (fileExt1 != std::wstring::npos) {
        if ((fileExt1 < 4) && (fileExt1) && (fileExt2 < windowsFilename.length() - 4)) {
            if (m_autoRemapFileExtensions) windowsFilename = windowsFilename.substr(fileExt1 + 1) + L"." + windowsFilename.substr(0, fileExt1);
            handleRemap(windowsPath, amigaFilename, windowsFilename);
        }
        else 
        if (fileExt1) {
            // Check if its one of the common amiga file extensions
            std::wstring ext = windowsFilename.substr(0, fileExt1);
            for (WCHAR& c : ext) c = towupper(c);
            std::string ex;
            wideToAnsi(ext, ex);
            // Handle any formats that we know about that got missed 
            if (CommonAmigaFileExtensions.find("." + ex + ".") != std::string::npos) {
                if (m_autoRemapFileExtensions) windowsFilename = windowsFilename.substr(fileExt1 + 1) + L"." + windowsFilename.substr(0, fileExt1);
                handleRemap(windowsPath, amigaFilename, windowsFilename);

            }
        }
    }

    // Save it, regardless, so we dont have to do this all again
    m_safeFilenameMap.insert(std::make_pair(amigaFilename, windowsFilename));
}

void DokanFileSystemAmiga::windowsFilenameToAmigaFilename(const std::wstring& windowsFilename, std::string& amigaFilename) {
    auto it = std::find_if(std::begin(m_safeFilenameMap), std::end(m_safeFilenameMap),
        [&windowsFilename](auto&& p) { return p.second == windowsFilename; });

    if (it != m_safeFilenameMap.end())
        amigaFilename = it->first;
    else {
        wideToAnsi(windowsFilename, amigaFilename);
    }
}

// These work on the full path
std::wstring DokanFileSystemAmiga::windowsPathToAmigaPath(const std::wstring& input) {
    std::wstring ret;

    size_t startSearch = 0;
    size_t i = input.find(L'\\', startSearch);
    while (i != std::wstring::npos) {
        std::string af;
        std::wstring afw;
        windowsFilenameToAmigaFilename(input.substr(startSearch, i - startSearch), af);
        ansiToWide(af, afw);
        if (ret.length()) ret += L'//';
        ret += afw;
        startSearch = i + 1;
        i = input.find(L'\\', startSearch);
    }
    if (startSearch < input.length()) {
        if (ret.length()) ret += L'//';
        ret += input.substr(startSearch);
    }

    return ret;
}

std::wstring DokanFileSystemAmiga::amigaToWindowsPath(const std::wstring& input) {
    std::wstring ret;

    size_t startSearch = 0;
    size_t i = input.find(L'/', startSearch);
    while (i != std::wstring::npos) {
        std::string af;
        std::wstring afw;

        wideToAnsi(input.substr(startSearch, i - startSearch), af);
        amigaFilenameToWindowsFilename(ret, af, afw);
        //ansiToWide(af, afw);
        if (ret.length()) ret += L'\\';
        ret += afw;
        startSearch = i + 1;
        i = input.find(L'/', startSearch);
    }
    if (startSearch < input.length()) {
        if (ret.length()) ret += L'\\';
        ret += input.substr(startSearch);
    }

    return ret;
}