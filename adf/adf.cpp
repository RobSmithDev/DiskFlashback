#include "adf.h"
#include "sectorCache.h"
#include "amiga_sectors.h"
#include <Shlobj.h>

#pragma comment(lib,"Shell32.lib")



// Return true if the drive has files open
bool fs::driveInUse() {
	return m_inUse.size();
}



RETCODE adfMountFlopButDontFail(struct AdfDevice* const dev)
{
	struct AdfVolume* vol;
	struct bRootBlock root;
	char diskName[35];

	dev->cylinders = 80;
	dev->heads = 2;
	if (dev->devType == DEVTYPE_FLOPDD)
		dev->sectors = 11;
	else
		dev->sectors = 22;

	vol = (struct AdfVolume*)malloc(sizeof(struct AdfVolume));
	if (!vol) return RC_ERROR;

	vol->mounted = TRUE;
	vol->firstBlock = 0;
	vol->lastBlock = (int32_t)(dev->cylinders * dev->heads * dev->sectors - 1);
	vol->rootBlock = (vol->lastBlock + 1 - vol->firstBlock) / 2;
	vol->blockSize = 512;
	vol->dev = dev;

	if (adfReadRootBlock(vol, (uint32_t)vol->rootBlock, &root) == RC_OK) {
		memset(diskName, 0, 35);
		memcpy_s(diskName, 35, root.diskName, root.nameLen);
		diskName[34] = '\0';  // make sure its null terminted
	}
	else diskName[0] = '\0';
	vol->volName = _strdup(diskName);

	if (dev->volList) {
		for (size_t i = 0; i < dev->nVol; i++) {
			if (dev->volList[i]) {
				if (dev->volList[i]->volName) free(dev->volList[i]->volName);
				free(dev->volList[i]);
			}
		}
		free(dev->volList);
	}
	dev->volList = (struct AdfVolume**)malloc(sizeof(struct AdfVolume*));
	if (!dev->volList) {
		free(vol);
		return RC_ERROR;
	}
	dev->volList[0] = vol;
	dev->nVol = 1;

	return RC_OK;
}


fs::fs(struct AdfDevice* adfDevice, struct AdfVolume* adfVolume, int volumeNumber, WCHAR driveLetter, bool readOnly) : 
	m_readOnly(readOnly), m_adfDevice(adfDevice), m_adfVolume(adfVolume), m_volumeNumber(volumeNumber)  {
	m_drive.resize(3);
	m_drive[0] = driveLetter;
	m_drive[1] = L':';
	m_drive[2] = L'\\';
}


// Release the drive so it can be used by other apps
void fs::releaseDrive() {
	if (m_remoteLockout) return;
	if (!m_adfDevice->nativeDev) return;
	SectorCacheEngine* io = (SectorCacheEngine*)m_adfDevice->nativeDev;
	if (!io) return;
	if (!io->isPhysicalDisk()) return;
	if (!setLocked(true)) {
		std::wstring t = L"Unable to release the " + getDriverName() + L" floppy drive.\nThere are files currently open";
		MessageBox(GetDesktopWindow(), t.c_str(), L"Drive in use", MB_OK | MB_ICONSTOP);
		return;
	}
	unmountVolume();
	m_remoteLockout = true;
	io->releaseDrive();
}

// Restore the drive after it was released
void fs::restoreDrive() {
	if (!m_remoteLockout) return;
	if (!m_adfDevice->nativeDev) return;
	SectorCacheEngine* io = (SectorCacheEngine*)m_adfDevice->nativeDev;
	if (!io) return;
	if (!io->isPhysicalDisk()) return;
	io->resetCache();
	io->restoreDrive();
	setLocked(false);
	remountVolume();
	m_remoteLockout = false;
}


// Special command to unmount the volume
void fs::unmountVolume() {
	if (m_adfVolume) {		
		adfUnMount(m_adfVolume);
		m_adfVolume = nullptr;
	}	
	SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATH, m_drive.c_str(), NULL);
	//refreshDriveInformation();
}

// Make windows realise a change occured
void fs::refreshDriveInformation() {
	
	
	//SHChangeNotify(SHCNE_FREESPACE, SHCNF_PATH, m_drive.c_str(), NULL);
}

// special command to re-mount the unmounted volume
void fs::remountVolume() {
	if (!m_adfVolume) {
		if (isPhysicalDevice()) adfMountFlopButDontFail(m_adfDevice);
		m_adfVolume = adfMount(m_adfDevice, m_volumeNumber, m_readOnly);
	}

	//SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATH, m_drive.c_str(), NULL);
	SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATH, m_drive.c_str(), NULL);
	SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATH, m_drive.c_str(), NULL);
	//SHChangeNotify(SHCNE_MEDIAINSERTED, SHCNF_PATH, m_drive.c_str(), NULL);
}

void fs::start() {
	DOKAN_OPTIONS dokan_options;
	ZeroMemory(&dokan_options, sizeof(DOKAN_OPTIONS));
	dokan_options.Version = DOKAN_VERSION;
	dokan_options.Options = DOKAN_OPTION_CURRENT_SESSION;
	std::wstring d = m_drive;
	if (d.length() < 3) d += L":\\";
	dokan_options.MountPoint = d.c_str();
	dokan_options.SingleThread = true;
	dokan_options.GlobalContext = reinterpret_cast<ULONG64>(this);
	dokan_options.SectorSize = m_adfVolume ? m_adfVolume->blockSize : 512;
	dokan_options.AllocationUnitSize = m_adfVolume ? m_adfVolume->blockSize : 512;
	dokan_options.Timeout = 5 * 60000; // 5 minutes

	// DOKAN_OPTION_WRITE_PROTECT
#ifdef _DEBUG
	//dokan_options.Options |= DOKAN_OPTION_STDERR;
#endif
	NTSTATUS status = DokanCreateFileSystem(&dokan_options, &fs_operations, &m_instance);

	switch (status) {
	case DOKAN_SUCCESS:
		break;
	case DOKAN_ERROR:
		throw std::runtime_error("Error");
	case DOKAN_DRIVE_LETTER_ERROR:
		throw std::runtime_error("Bad Drive letter");
	case DOKAN_DRIVER_INSTALL_ERROR:
		throw std::runtime_error("Can't install driver");
	case DOKAN_START_ERROR:
		throw std::runtime_error("Driver something wrong");
	case DOKAN_MOUNT_ERROR:
		throw std::runtime_error("Can't assign a drive letter");
	case DOKAN_MOUNT_POINT_ERROR:
		throw std::runtime_error("Mount point error");
	case DOKAN_VERSION_ERROR:
		throw std::runtime_error("Version error");
	default:
		throw std::runtime_error("Unknown error");
	}
}


// Returns if disk is locked and cannot be read
bool fs::isLocked() {
	return (m_adfDevice->nativeDev) && (((SectorCacheEngine*)m_adfDevice->nativeDev)->isAccessLocked());
}

// Returns FALSE if files are open
bool fs::setLocked(bool enableLock) {
	if (m_inUse.size()) return false;
	if (!m_adfDevice->nativeDev) return false;
	((SectorCacheEngine*)m_adfDevice->nativeDev)->flushWriteCache();
	((SectorCacheEngine*)m_adfDevice->nativeDev)->setLocked(enableLock);
	return true;
}

// Returns TRUE if write protected
bool fs::isWriteProtected() {
	return m_readOnly || ((m_adfDevice->nativeDev) && (((SectorCacheEngine*)m_adfDevice->nativeDev)->isDiskWriteProtected()));
}

// Returns TRUE if theres a disk in the drive
bool fs::isDiskInDrive() {
	if (!m_adfDevice->nativeDev) return true;
	return ((SectorCacheEngine*)m_adfDevice->nativeDev)->isDiskPresent();
}

// Returns TRUE if this is a real disk
bool fs::isPhysicalDevice() {
	if (!m_adfDevice->nativeDev) return true;
	return ((SectorCacheEngine*)m_adfDevice->nativeDev)->isPhysicalDisk();
}

// Returns the name of the driver used for FloppyBridge
std::wstring fs::getDriverName() {
	if (!m_adfDevice->nativeDev) return L"Unknown";
	return ((SectorCacheEngine*)m_adfDevice->nativeDev)->getDriverName();
}

// Install bootblock
bool fs::installBootBlock() {
	if (m_remoteLockout) return false;
	if (!m_adfVolume) return false;
	const std::string appName = "Installed with " APPLICATION_NAME;

	// 1024 bytes is required
	uint8_t* mem = (uint8_t*)malloc(1024);
	if (!mem) return false;
	memset(mem, 0, 1024);

	memset(mem, 0, 1024);
	if (isFFS(m_adfVolume->dosType)) {
		size_t size = sizeof(bootblock_ofs) / sizeof(*bootblock_ofs);
		memcpy_s(mem, 1024, bootblock_ofs, size);
		strcpy_s((char*)(mem + size + 8), 1024 - (size + 8), appName.c_str());
	}
	else {
		size_t size = sizeof(bootblock_ffs) / sizeof(*bootblock_ffs);
		memcpy_s(mem, 1024, bootblock_ffs, size);
		strcpy_s((char*)(mem + size + 8), 1024 - (size + 8), appName.c_str());
	}
	
	if (isWriteProtected()) return false;
	// Nothing writes to where thr boot block is so it's safe to do this
	bool ok = adfInstallBootBlock(m_adfVolume, mem) == RC_OK;

	free(mem);

	return ok;
}

// Add some hacks to the registery to make the drive context menu work
void fs::applyRegistryAction(const std::wstring registeryKeyName, const std::wstring menuLabel, const int iconIndex, const std::wstring& commandParams) {
	WCHAR path[256];
	swprintf_s(path, L"Software\\Classes\\Drive\\shell\\%s_%c", registeryKeyName.c_str(), m_drive[0]);
	RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, menuLabel.c_str(), (DWORD)(menuLabel.length() * 2));
	RegSetKeyValue(HKEY_CURRENT_USER, path, L"AppliesTo", REG_SZ, m_drive.c_str(), (DWORD)(m_drive.length() * 2));
	RegSetKeyValue(HKEY_CURRENT_USER, path, L"MultiSelectModel", REG_SZ, L"Single", 6 * 2);
	const std::wstring iconNumber = L"\"" + mainEXE + L"\", " + std::to_wstring(iconIndex);
	RegSetKeyValue(HKEY_CURRENT_USER, path, L"Icon", REG_SZ, iconNumber.c_str(), (DWORD)(iconNumber.length() * 2));
	std::wstring cmd = L"\"" + mainEXE + L"\" CONTROL "+ m_drive.substr(0,1)+L" "+commandParams;
	wcscat_s(path, L"\\command");
	RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, cmd.c_str(), (DWORD)(cmd.length() * 2));
}

// Remove those hacks
void fs::removeRegisteryAction(const std::wstring registeryKeyName) {
	WCHAR path[256];
	swprintf_s(path, L"Software\\Classes\\Drive\\shell\\%s_%c", registeryKeyName.c_str(), m_drive[0]);
	std::wstring path2 = std::wstring(path) + L"\\command";
	RegDeleteKey(HKEY_CURRENT_USER, path2.c_str());
	RegDeleteKey(HKEY_CURRENT_USER, path);
}

// Add data to the context menu for ADF
void fs::controlADFMenu(bool add) {
	WCHAR path[128];	

	// Add a entry to the ADF context menu
	LONG len = 128;
	if (FAILED(RegQueryValue(HKEY_CURRENT_USER, L"Software\\Classes\\.adf", path, &len))) {
		wcscpy_s(path, L"amiga.adf.file");
		RegSetValue(HKEY_CURRENT_USER, L"Software\\Classes\\.adf", REG_SZ, path, wcslen(path) * 2);
	}

	std::wstring clsRoot = L"Software\\Classes\\" + std::wstring(path) + L"\\shell";
	if (add) {
		std::wstring cmd = L"\"" + mainEXE + L"\" CONTROL " + m_drive.substr(0, 1) + L" 2DISK \"%1\"";
		clsRoot += L"\\Copy to Disk...";
		const std::wstring iconNumber = L"\"" + mainEXE + L"\", " + std::to_wstring(0);
		RegSetKeyValue(HKEY_CURRENT_USER, clsRoot.c_str(), L"Icon", REG_SZ, iconNumber.c_str(), iconNumber.length() * 2);
		clsRoot += L"\\command";
		RegSetValue(HKEY_CURRENT_USER, clsRoot.c_str(), REG_SZ, cmd.c_str(), cmd.length() * 2);

		//"Icon" = "C:\\Program Files\\ConsoleZ\\Console.exe"
	}
	else {
		std::wstring clsRoot2 = clsRoot + L"\\Copy to Disk...";
		std::wstring clsRoot3 = clsRoot2 + L"\\command";
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot3.c_str());
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot2.c_str());
	}
}


// Add data to the context menu for DMS
void fs::controlDMSMenu(bool add) {
	WCHAR path[128];

	// Add a entry to the ADF context menu
	LONG len = 128;
	if (FAILED(RegQueryValue(HKEY_CURRENT_USER, L"Software\\Classes\\.dms", path, &len))) {
		wcscpy_s(path, L"amiga.dms.file");
		RegSetValue(HKEY_CURRENT_USER, L"Software\\Classes\\.dms", REG_SZ, path, wcslen(path) * 2);
	}

	std::wstring clsRoot = L"Software\\Classes\\" + std::wstring(path) + L"\\shell";
	if (add) {
		std::wstring cmd = L"\"" + mainEXE + L"\" CONTROL " + m_drive.substr(0, 1) + L" 2DISK \"%1\"";
		clsRoot += L"\\Copy to Disk...";
		const std::wstring iconNumber = L"\"" + mainEXE + L"\", " + std::to_wstring(0);
		RegSetKeyValue(HKEY_CURRENT_USER, clsRoot.c_str(), L"Icon", REG_SZ, iconNumber.c_str(), iconNumber.length() * 2);
		clsRoot += L"\\command";
		RegSetValue(HKEY_CURRENT_USER, clsRoot.c_str(), REG_SZ, cmd.c_str(), cmd.length() * 2);

		//"Icon" = "C:\\Program Files\\ConsoleZ\\Console.exe"
	}
	else {
		std::wstring clsRoot2 = clsRoot + L"\\Copy to Disk...";
		std::wstring clsRoot3 = clsRoot2 + L"\\command";
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot3.c_str());
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot2.c_str());
	}
}

void fs::mounted(const std::wstring& mountPoint, bool wasMounted) {
	if (wasMounted) m_drive = mountPoint;
	if (m_drive.empty()) return;
	m_drive[0] = towupper(m_drive[0]);

	WCHAR path[128];
	swprintf_s(path, L"Software\\Classes\\Applications\\Explorer.exe\\Drives\\%s\\DefaultIcon", m_drive.substr(0, 1).c_str());

	// Clear anything previously there
	removeRegisteryAction(L"ADFMounterSystemBB");
	removeRegisteryAction(L"ADFMounterSystemFormat");
	removeRegisteryAction(L"ADFMounterSystemEject");
	removeRegisteryAction(L"ADFMounterSystemCopy");


	RegDeleteKeyValue(HKEY_CURRENT_USER, path, NULL);
	RegDeleteKey(HKEY_CURRENT_USER, path);
	path[wcslen(path) - 12] = L'\0';
	RegDeleteKey(HKEY_CURRENT_USER, path);
	SectorCacheEngine* cache = (SectorCacheEngine*)m_adfDevice->nativeDev;
	const bool physicalDisk = m_adfDevice && m_adfDevice->nativeDev && (cache->isPhysicalDisk());
	const bool copyToADF = m_adfDevice && m_adfDevice->nativeDev && (cache->allowCopyToADF());
	if (physicalDisk) {
		controlADFMenu(false);
		controlDMSMenu(false);
	}

	swprintf_s(path, L"Software\\Classes\\Applications\\Explorer.exe\\Drives\\%s\\DefaultIcon", m_drive.substr(0, 1).c_str());

	if (wasMounted) {
		RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, mainEXE.c_str(), (DWORD)(mainEXE.length() * 2));
		if (cache && !cache->isDiskWriteProtected() && physicalDisk)
			applyRegistryAction(L"ADFMounterSystemFormat", L"Form&at...", 0,L"FORMAT");

		if (copyToADF) {
			applyRegistryAction(L"ADFMounterSystemCopy", L"&Copy to ADF...", 0, L"BACKUP");
			controlADFMenu(true);
			controlDMSMenu(true);
		}			
		if (m_adfVolume && cache && !cache->isDiskWriteProtected()) {
			if (m_adfVolume->dev->devType == DEVTYPE_FLOPDD || m_adfVolume->dev->devType == DEVTYPE_FLOPHD) {
				applyRegistryAction(L"ADFMounterSystemBB", L"&Install Bootblock...", 0, L"BB");
			}
		}
		applyRegistryAction(L"ADFMounterSystemEject", physicalDisk ? L"&Remove Drive" : L"&Eject", 0, L"EJECT");
	}
}

bool fs::isRunning() {
	return DokanIsFileSystemRunning(m_instance);
}

void fs::wait(DWORD timeout) {
	DokanWaitForFileSystemClosed(m_instance, timeout);
}

void fs::stop() {
	if (m_instance) {
		DokanCloseHandle(m_instance);		
		m_instance = 0;
		m_drive = L"";
		if (m_adfVolume) adfUnMount(m_adfVolume);
		m_adfVolume = nullptr;
	}
}


