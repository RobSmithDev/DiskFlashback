#include "adf.h"


fs::fs(struct AdfDevice* adfDevice, struct AdfVolume* adfVolume, WCHAR driveLetter) : m_adfDevice(adfDevice), m_adfVolume(adfVolume) {
	m_drive.resize(3);
	m_drive[0] = driveLetter;
	m_drive[1] = L':';
	m_drive[2] = L'\\';
}

void fs::start() {
	DOKAN_OPTIONS dokan_options;
	ZeroMemory(&dokan_options, sizeof(DOKAN_OPTIONS));
	dokan_options.Version = DOKAN_VERSION;
	dokan_options.Options = DOKAN_OPTION_REMOVABLE | DOKAN_OPTION_CURRENT_SESSION;
	dokan_options.MountPoint = m_drive.c_str();
	dokan_options.SingleThread = true;
	dokan_options.Timeout = 0;
	dokan_options.GlobalContext = reinterpret_cast<ULONG64>(this);
	// DOKAN_OPTION_WRITE_PROTECT
#ifdef _DEBUG
	//dokan_options.Options |= DOKAN_OPTION_STDERR | DOKAN_OPTION_DEBUG;
		//dokan_options.Options |= DOKAN_OPTION_DISPATCH_DRIVER_LOGS;
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


bool fs::isRunning() {
	return DokanIsFileSystemRunning(m_instance);	
}

void fs::wait(DWORD timeout) {
	DokanWaitForFileSystemClosed(m_instance, timeout);
}

void fs::stop() { 
	if (m_instance) {
		DokanCloseHandle(m_instance);
		DokanRemoveMountPoint(m_drive.c_str());
		m_instance = 0;
		m_drive = L"";
		adfUnMount(m_adfVolume);
		m_adfVolume = nullptr;
	}
}

// Handles fixing filenames so they're amiga compatable
void fs::amigaFilenameToWindowsFilename(const std::string& amigaFilename, std::wstring& windowsFilename) {
	auto fle = m_safeFilenameMap.find(amigaFilename);
	if (fle != m_safeFilenameMap.end()) {
		windowsFilename = fle->second;
		return;
	}

	std::string name = amigaFilename;

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

	ansiToWide(name, windowsFilename);

	if (amigaFilename != name) {
		m_safeFilenameMap.insert(std::make_pair(amigaFilename, windowsFilename));
	}
}

void fs::windowsFilenameToAmigaFilename(const std::wstring& windowsFilename, std::string& amigaFilename) {
	auto it = std::find_if(std::begin(m_safeFilenameMap), std::end(m_safeFilenameMap),
		[&windowsFilename](auto&& p) { return p.second == windowsFilename; });
	if (it != m_safeFilenameMap.end()) 
		amigaFilename = it->first;
	else {
		wideToAnsi(windowsFilename, amigaFilename);
	}
}



void wideToAnsi(const std::wstring& wstr, std::string& str) {

	int size = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
	if (size) {
		str.resize(size);
		WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &str[0], size, NULL, NULL);
		str.resize(size - 1);
	}
}

void ansiToWide(const std::string& wstr, std::wstring& str) {
	int size = MultiByteToWideChar(CP_ACP, 0, wstr.c_str(), -1, NULL, 0);
	if (size) {
		str.resize(size * 2);
		MultiByteToWideChar(CP_ACP, 0, wstr.c_str(), -1, &str[0], size * 2);
	}
}

