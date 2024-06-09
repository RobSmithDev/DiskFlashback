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

#include <dokan/dokan.h>
#include <string>
#include "dokaninterface.h"
#include <map>
#include <unordered_map>

// A class with all of the Dokan commands needed
class DokanFileSystemAmiga : public DokanFileSystemBase {
protected:   
    // Simple nasty class to auto release (so I don't forget) details about an active file i/o occuring. 
    // Everything is single threadded so this is OK
    class ActiveFileIO {
    private:
        DokanFileSystemAmiga* m_owner;
    public:
        ActiveFileIO(DokanFileSystemAmiga* owner);
        // Remove copy constructor
        ActiveFileIO(const ActiveFileIO&) = delete;
        // Add Move constructor
        ActiveFileIO(ActiveFileIO&& source) noexcept;
        ~ActiveFileIO();
    };
    friend class ActiveFileIO;
private:

    bool m_autoRemapFileExtensions = false;  // remap mod.* to *.mod for example

    // Reverse mapping for badly (non-windows) named files
    std::map<std::string, std::wstring> m_safeFilenameMap;
    std::map<std::wstring, std::string> m_specialRenameMap;

protected:
    // Handles fixing filenames so they're amiga compatable - returns TRUE if the name changed
    void amigaFilenameToWindowsFilename(const std::wstring& windowsPath, const std::string& amigaFilename, std::wstring& windowsFilename);
    void windowsFilenameToAmigaFilename(const std::wstring& windowsFilename, std::string& amigaFilename);

    std::wstring windowsPathToAmigaPath(const std::wstring& input);
    std::wstring amigaToWindowsPath(const std::wstring& input);

    // Handle a note about the remap of file extension
    void handleRemap(const std::wstring& windowsPath, const std::string& amigaFilename, std::wstring& windowsFilename);

    // Sets a current file info block as active (or NULL for not) so DokanResetTimeout can be called if needed
    void setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo);
    void clearFileIO();
    // Let the system know I/O is currently happenning.  ActiveFileIO must be kepyt in scope until io is complete
    ActiveFileIO notifyIOInUse(PDOKAN_FILE_INFO dokanfileinfo); 

public:
    DokanFileSystemAmiga(DokanFileSystemManager* owner, bool autoRename);
    void changeAutoRename(bool autoRename);
    virtual void resetFileSystem();
};

