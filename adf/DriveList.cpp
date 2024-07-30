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

 // A lot of the code here was re-written/refactored/modified from the code in hardfile_win32.cpp which is part of the WinUAE source code

#include "DriveList.h"
#include <SetupAPI.h>
#include <Cfgmgr32.h>
#include <string.h>
#include <ntddscsi.h>
#include "dokaninterface.h"

#include "adf_nativedriver.h"

#include "ADFlib/src/adflib.h"
#include "PFS3Lib/pfs3.h"


#pragma comment(lib, "Setupapi.lib")

#define CA "Commodore\0Amiga\0"
#define	INQ_DASD	0x00
#define INQUIRY_LEN 240
#define VALIDOFFSET(offset, mx) (offset > 0 && offset < mx)
#define ISWHITESPACE(x) ((x==L'\t')||(x==L'\r')||(x==L'\n')||(x==L' '))
#define ISNOMEDIAERROR(err) (err == ERROR_NOT_READY || err == ERROR_MEDIA_CHANGED || err == ERROR_NO_MEDIA_IN_DRIVE || err == ERROR_DEV_NOT_EXIST || err == ERROR_BAD_NET_NAME || err == ERROR_WRONG_DISK)

#define DEVICE_INDEX_IGNORE -2
#define DEVICE_INDEX_ADD -1

typedef struct _SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER {
	SCSI_PASS_THROUGH_DIRECT spt;
	ULONG Filler;
	UCHAR SenseBuf[32];
} SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER;

static bool hdReadATA(HANDLE h, uint8_t* ideregs, uint8_t* datap, int length) {	
	DWORD size = sizeof(ATA_PASS_THROUGH_EX) + length;
	uint8_t* b = (uint8_t*)malloc(size);
	if (!b) return false;
	ATA_PASS_THROUGH_EX* ata = (ATA_PASS_THROUGH_EX*)b;
	uint8_t* data = b + sizeof(ATA_PASS_THROUGH_EX);
	ata->Length = sizeof(ATA_PASS_THROUGH_EX);
	ata->DataTransferLength = length;
	ata->TimeOutValue = 10;
	ata->AtaFlags = ATA_FLAGS_DRDY_REQUIRED | ATA_FLAGS_DATA_IN;
	IDEREGS* ir = (IDEREGS*)ata->CurrentTaskFile;
	ir->bFeaturesReg = ideregs[1];
	ir->bSectorCountReg = ideregs[2];
	ir->bSectorNumberReg = ideregs[3];
	ir->bCylLowReg = ideregs[4];
	ir->bCylHighReg = ideregs[5];
	ir->bDriveHeadReg = ideregs[6];
	ir->bCommandReg = ideregs[7];
	ata->DataBufferOffset = data - b;
	DWORD r;
	if (!DeviceIoControl(h, IOCTL_ATA_PASS_THROUGH, b, size, b, size, &r, NULL)) {
		free(b);
		return false;
	}
	memcpy(datap, data, length);
	free(b);
	return true;
}

// Checks to see if the device identity is a Cylinder-Head-Sector device - see https://en.wikipedia.org/wiki/Cylinder-head-sector
static bool isCHS(uint8_t* identity) {
	if (!identity[0] && !identity[1]) return false;
	uint8_t* d = identity;

	// C/H/S = zeros?
	if ((!d[2] && !d[3]) || (!d[6] && !d[7]) || (!d[12] && !d[13])) return false;
	
	// LBA = zero?
	if (d[60 * 2 + 0] || d[60 * 2 + 1] || d[61 * 2 + 0] || d[61 * 2 + 1]) return false;
	
	uint16_t v = (d[49 * 2 + 0] << 8) | (d[49 * 2 + 1] << 0);
	return !(v & (1 << 9)); // LBA not supported?	
}

// Trim left and right extra characters
static void stringTrim(std::wstring& string) {
	size_t index = 0;
	while ((index<string.length()) && (ISWHITESPACE(string[index]))) index++;
	string = string.substr(index);
	if (string.empty()) return;
	index = string.length() - 1;
	while ((string.length()>index) && (ISWHITESPACE(string[index]))) index--;
	string = string.substr(0, index+1);
}

static bool hdGetMetaATA(HANDLE h, bool atapi, uint8_t* datap) {
	DWORD r, size;
	ATA_PASS_THROUGH_EX* ata;

	size = sizeof(ATA_PASS_THROUGH_EX) + 512;
	uint8_t* b = (uint8_t*)malloc(size);
	if (!b) return false;
	ata = (ATA_PASS_THROUGH_EX*)b;
	uint8_t* data = b + sizeof(ATA_PASS_THROUGH_EX);
	ata->Length = sizeof(ATA_PASS_THROUGH_EX);
	ata->DataTransferLength = 512;
	ata->TimeOutValue = 10;
	ata->AtaFlags = ATA_FLAGS_DRDY_REQUIRED | ATA_FLAGS_DATA_IN;
	IDEREGS* ir = (IDEREGS*)ata->CurrentTaskFile;
	ir->bCommandReg = atapi ? ATAPI_ID_CMD : ID_CMD;
	ata->DataBufferOffset = data - b;
	if (!DeviceIoControl(h, IOCTL_ATA_PASS_THROUGH, b, size, b, size, &r, NULL)) {
		free(b);
		return false;
	}
	memcpy(datap, data, 512);
	free(b);
	return true;
}

static int doScsiIn(HANDLE h, const uint8_t* cdb, int cdblen, uint8_t* in, int insize, bool fast) {
	SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
	DWORD status, returned;

	memset(&swb, 0, sizeof swb);
	swb.spt.Length = sizeof(SCSI_PASS_THROUGH);
	swb.spt.CdbLength = cdblen;
	swb.spt.DataIn = insize > 0 ? SCSI_IOCTL_DATA_IN : 0;
	swb.spt.DataTransferLength = insize;
	swb.spt.DataBuffer = in;
	swb.spt.TimeOutValue = fast ? 2 : 10 * 60;
	swb.spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, SenseBuf);
	swb.spt.SenseInfoLength = 32;
	memcpy(swb.spt.Cdb, cdb, cdblen);

	status = DeviceIoControl(h, IOCTL_SCSI_PASS_THROUGH_DIRECT, &swb, sizeof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER), &swb, sizeof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER), &returned, NULL);
	if (!status) {
		if (GetLastError() == ERROR_SEM_TIMEOUT) return -2; // stupid hardware
		return -1;
	}
	else if (swb.spt.ScsiStatus) return -1;
	return swb.spt.DataTransferLength;
}

static void sl(uint8_t* d, int o) {
	o *= 2;
	uint16_t t = (d[o + 0] << 8) | d[o + 1];
	d[o + 0] = d[o + 2];
	d[o + 1] = d[o + 3];
	d[o + 2] = t >> 8;
	d[o + 3] = (uint8_t)t;
}
static void ql(uint8_t* d, int o) {
	sl(d, o + 1);
	o *= 2;
	uint16_t t = (d[o + 0] << 8) | d[o + 1];
	d[o + 0] = d[o + 6];
	d[o + 1] = d[o + 7];
	d[o + 6] = t >> 8;
	d[o + 7] = (uint8_t)t;
}
void ataByteSwapIdentity(uint8_t* d) {
	for (int i = 0; i < 512; i += 2) {
		uint8_t t = d[i + 0];
		d[i + 0] = d[i + 1];
		d[i + 1] = t;
	}
	sl(d, 7);
	sl(d, 57);
	sl(d, 60);
	sl(d, 98);
	sl(d, 117);
	sl(d, 210);
	sl(d, 212);
	sl(d, 215);
	ql(d, 100);
	ql(d, 230);
}
static void toCHS(uint8_t* data, int64_t offset, int* cp, int* hp, int* sp) {
	int c, h, s;
	c = (data[1 * 2 + 0] << 8) | (data[1 * 2 + 1] << 0);
	h = (data[3 * 2 + 0] << 8) | (data[3 * 2 + 1] << 0);
	s = (data[6 * 2 + 0] << 8) | (data[6 * 2 + 1] << 0);
	if (offset >= 0) {
		offset /= 512;
		c = (int)(offset / (h * s));
		offset -= c * h * s;
		h = (int)(offset / s);
		offset -= h * s;
		s = (int)(offset + 1);
	}
	*cp = c;
	*hp = h;
	*sp = s;
}

// Fetch disk dignature and partition style (MBR, GPT, RAW)
static bool getSignatureFromHandle(HANDLE h, DWORD& signature, DWORD& partitionStyle) {
	DWORD written;
	DWORD outsize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + sizeof(PARTITION_INFORMATION_EX) * 32;
	DRIVE_LAYOUT_INFORMATION_EX* dli = (DRIVE_LAYOUT_INFORMATION_EX*)malloc(outsize);
	if (!dli) return 0;
	if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, dli, outsize, &written, NULL)) {
		signature = dli->Mbr.Signature;
		partitionStyle = dli->PartitionStyle;
		free(dli);
		return true;
	}

	if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT, NULL, 0, dli, outsize, &written, NULL)) {
		DRIVE_LAYOUT_INFORMATION* dli2 = (DRIVE_LAYOUT_INFORMATION*)dli;
		signature = dli2->Signature;
		partitionStyle = PARTITION_STYLE_MBR;
		free(dli);
		return true;
	}
	free(dli);
	return false;
}

static int isMounted(HANDLE hd) {
	int mounted;

	DWORD signature, partitionStyle;
	if (!getSignatureFromHandle(hd, signature, partitionStyle)) return 0;
	if (partitionStyle == PARTITION_STYLE_GPT) return -1;
	if (partitionStyle == PARTITION_STYLE_RAW) return 0;
	mounted = 0;

	WCHAR volname[MAX_PATH];
	HANDLE h = FindFirstVolume(volname, MAX_PATH);
	while (h != INVALID_HANDLE_VALUE && !mounted) {
		if (volname[wcslen(volname) - 1] == '\\') volname[wcslen(volname) - 1] = 0;
		HANDLE d = CreateFile(volname, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (d != INVALID_HANDLE_VALUE) {
			DWORD isntfs, outsize, written;
			isntfs = 0;
			if (DeviceIoControl(d, FSCTL_IS_VOLUME_MOUNTED, NULL, 0, NULL, 0, &written, NULL)) {
				VOLUME_DISK_EXTENTS* vde;
				NTFS_VOLUME_DATA_BUFFER ntfs;
				if (DeviceIoControl(d, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &ntfs, sizeof ntfs, &written, NULL)) isntfs = 1;
				outsize = sizeof(VOLUME_DISK_EXTENTS) + sizeof(DISK_EXTENT) * 32;
				vde = (VOLUME_DISK_EXTENTS*)malloc(outsize);
				if (!vde) return 0;

				if (DeviceIoControl(d, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde, outsize, &written, NULL)) {
					for (DWORD i = 0; i < vde->NumberOfDiskExtents; i++) {
						WCHAR pdrv[MAX_PATH];
						HANDLE ph;
						swprintf_s(pdrv, L"\\\\.\\PhysicalDrive%d", vde->Extents[i].DiskNumber);
						ph = CreateFile(pdrv, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						if (ph != INVALID_HANDLE_VALUE) {
							DWORD signature2;
							if (getSignatureFromHandle(ph, signature2, partitionStyle)) {
								if (signature == signature2 && partitionStyle == PARTITION_STYLE_MBR)
									mounted = isntfs ? -1 : 1;
							}
							CloseHandle(ph);
						}
					}
				}
				free(vde);
			}
			CloseHandle(d);
		}
		if (!FindNextVolume(h, volname, sizeof volname / sizeof(TCHAR))) break;
	}
	FindVolumeClose(h);
	return mounted;
}

bool hdGetMetaHack(HANDLE h, uint8_t* data, uint8_t* inq, CDriveList::CDevice& device) {
	if (device.usbVID == 0x152d && (device.usbPID == 0x2329 || device.usbPID == 0x2336 || device.usbPID == 0x2338 || device.usbPID == 0x2339)) {
		uint8_t cmd[16];
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = 0xdf;
		cmd[1] = 0x10;
		cmd[4] = 1;
		cmd[6] = 0x72;
		cmd[7] = 0x0f;
		cmd[11] = 0xfd;
		if (doScsiIn(h, cmd, 12, data + 32, 1, true) < 0) {
			memset(data, 0, 512);
			return false;
		}
		if (!(data[32] & 0x40) && !(data[32] & 0x04)) {
			memset(data, 0, 512);
			return false;
		}
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = 0xdf;
		cmd[1] = 0x10;
		cmd[3] = 512 >> 8;
		cmd[10] = 0xa0 | ((data[32] & 0x40) ? 0x10 : 0x00);
		cmd[11] = ID_CMD;
		if (doScsiIn(h, cmd, 12, data, 512, true) < 0) {
			memset(data, 0, 512);
			return false;
		}
		return true;
	}
	return false;
}

bool getStorageInfo(CDriveList::CDevice& drive, const STORAGE_DEVICE_NUMBER sdnp) {
	int idx;
	const GUID* di = &GUID_DEVINTERFACE_DISK;
	
	DWORD vpm[3] = { 0xffffffff , 0xffffffff , 0xffffffff };
	HDEVINFO hIntDevInfo = SetupDiGetClassDevs(di, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (!hIntDevInfo) return false;
	idx = -1;
	for (;;) {
		PSP_DEVICE_INTERFACE_DETAIL_DATA pInterfaceDetailData = NULL;
		SP_DEVICE_INTERFACE_DATA interfaceData = { 0 };
		SP_DEVINFO_DATA deviceInfoData = { 0 };
		DWORD dwRequiredSize, returnedLength;

		idx++;
		interfaceData.cbSize = sizeof(interfaceData);
		if (!SetupDiEnumDeviceInterfaces(hIntDevInfo, NULL, di, idx, &interfaceData)) break;
		dwRequiredSize = 0;
		if (!SetupDiGetDeviceInterfaceDetail(hIntDevInfo, &interfaceData, NULL, 0, &dwRequiredSize, NULL)) {
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) break;
			if (dwRequiredSize <= 0) break;
		}
		pInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)LocalAlloc(LPTR, dwRequiredSize);
		pInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
		deviceInfoData.cbSize = sizeof(deviceInfoData);
		if (!SetupDiGetDeviceInterfaceDetail(hIntDevInfo, &interfaceData, pInterfaceDetailData, dwRequiredSize, &dwRequiredSize, &deviceInfoData)) {
			LocalFree(pInterfaceDetailData);
			continue;
		}
		HANDLE hDev = CreateFile(pInterfaceDetailData->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hDev == INVALID_HANDLE_VALUE) {
			LocalFree(pInterfaceDetailData);
			continue;
		}
		LocalFree(pInterfaceDetailData);
		STORAGE_DEVICE_NUMBER sdn;
		if (!DeviceIoControl(hDev, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &returnedLength, NULL)) {
			CloseHandle(hDev);
			continue;
		}
		CloseHandle(hDev);
		if (sdnp.DeviceType != sdn.DeviceType || sdnp.DeviceNumber != sdn.DeviceNumber) continue;

		WCHAR pszDeviceInstanceId[1024];
		DEVINST dnDevInstParent, dwDeviceInstanceIdSize;
		dwDeviceInstanceIdSize = sizeof(pszDeviceInstanceId) / sizeof(WCHAR);
		if (!SetupDiGetDeviceInstanceId(hIntDevInfo, &deviceInfoData, pszDeviceInstanceId, dwDeviceInstanceIdSize, &dwRequiredSize)) continue;

		if (CM_Get_Parent(&dnDevInstParent, deviceInfoData.DevInst, 0) == CR_SUCCESS) {
			WCHAR szDeviceInstanceID[MAX_DEVICE_ID_LEN];
			if (CM_Get_Device_ID(dnDevInstParent, szDeviceInstanceID, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
				const static LPCTSTR arPrefix[3] = { TEXT("VID_"), TEXT("PID_"), TEXT("MI_") };
				LPTSTR pszNextToken;
				LPTSTR pszToken = wcstok_s(szDeviceInstanceID, TEXT("\\#&"), &pszNextToken);
				while (pszToken != NULL) {
					for (int j = 0; j < 3; j++) {
						if (wcsncmp(pszToken, arPrefix[j], lstrlen(arPrefix[j])) == 0) {
							wchar_t* endptr;
							vpm[j] = wcstol(pszToken + lstrlen(arPrefix[j]), &endptr, 16);
						}
					}
					pszToken = wcstok_s(NULL, TEXT("\\#&"), &pszNextToken);
				}
			}
		}
		if (vpm[0] != 0xffffffff && vpm[1] != 0xffffffff) break;
	}
	SetupDiDestroyDeviceInfoList(hIntDevInfo);
	if (vpm[0] == 0xffffffff || vpm[1] == 0xffffffff) return false;

	drive.usbVID = vpm[0];
	drive.usbPID = vpm[1];
	return true;
}

std::wstring extractToUnicode(const uint8_t* input, const int length) {
	std::string ret;
	ret.resize(length);
	memcpy_s(&ret[0], ret.length(), input, length);
	auto pos = ret.find('\0');
	if (pos != std::string::npos) ret = ret.substr(0, pos);
	std::wstring retw;
	ansiToWide(ret, retw);
	return retw;
}

// -2 means error (DEVICE_INDEX_IGNORE)
// -1 means OK and new (DEVICE_INDEX_ADD)
// >=0 means it replaced the index at that position
int16_t getStorageProperty(void* outBuf, int returnedLength, CDriveList::CDevice& device, bool ignoreDuplicates, CDriveList::DeviceList& deviceList) {
	PSTORAGE_DEVICE_DESCRIPTOR devDesc = (PSTORAGE_DEVICE_DESCRIPTOR)outBuf;
	const ULONG size = devDesc->Version;
	const ULONG size2 = devDesc->Size > (ULONG)returnedLength ? (ULONG)returnedLength : devDesc->Size;
	if (offsetof(STORAGE_DEVICE_DESCRIPTOR, CommandQueueing) > size) return DEVICE_INDEX_IGNORE;
	if (devDesc->DeviceType != INQ_DASD) return DEVICE_INDEX_IGNORE;
	const uint8_t* p = (uint8_t*)outBuf;

	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, VendorIdOffset) && VALIDOFFSET(devDesc->VendorIdOffset, size2) && p[devDesc->VendorIdOffset]) 
		device.vendorId = extractToUnicode(&p[devDesc->VendorIdOffset], returnedLength);
	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, ProductIdOffset) && VALIDOFFSET(devDesc->ProductIdOffset, size2) && p[devDesc->ProductIdOffset]) 
		device.productId = extractToUnicode(&p[devDesc->ProductIdOffset], returnedLength);
	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, ProductRevisionOffset) && VALIDOFFSET(devDesc->ProductRevisionOffset, size2) && p[devDesc->ProductRevisionOffset]) 
		device.productRev = extractToUnicode(&p[devDesc->ProductRevisionOffset], returnedLength);
	if (size > offsetof(STORAGE_DEVICE_DESCRIPTOR, SerialNumberOffset) && VALIDOFFSET(devDesc->SerialNumberOffset, size2) && p[devDesc->SerialNumberOffset]) 
		device.productSerial = extractToUnicode(&p[devDesc->SerialNumberOffset], returnedLength);

	stringTrim(device.vendorId);
	stringTrim(device.productId);
	stringTrim(device.productRev);
	stringTrim(device.productSerial);

	if (!device.vendorId.empty()) device.deviceName += device.vendorId;

	if (!device.productId.empty()) {
		if (!device.deviceName.empty()) device.deviceName += L" ";
		device.deviceName += device.productId;
	}
	if (!device.productRev.empty()) {
		if (!device.deviceName.empty()) device.deviceName += L" ";
		device.deviceName += device.productRev;
	}
	if (!device.productSerial.empty()) {
		if (!device.deviceName.empty()) device.deviceName += L" ";
		device.deviceName += device.productSerial;
	}
	if (device.deviceName.empty()) device.deviceName = device.devicePath;
	device.isRemovable = devDesc->RemovableMedia;

	while ((!device.deviceName.empty()) && (device.deviceName[device.deviceName.length() - 1] == L':'))  device.deviceName = device.deviceName.substr(0, device.deviceName.length() - 1);
	for (WCHAR& c : device.deviceName) if (c == L':') c = L'_';

	device.busType = (CDriveList::StorageTypeBus)devDesc->BusType;
	if (ignoreDuplicates) {
		//if (!device.isRemovable) return DEVICE_INDEX_IGNORE;
		for (size_t index = 0; index < deviceList.size(); index++)
			if (deviceList[index].deviceName == device.deviceName) {
				if (deviceList[index].danger == CDriveList::DangerType::dtBad) return (int16_t)index;
				return DEVICE_INDEX_IGNORE;
			}
	}
	return DEVICE_INDEX_ADD;
}

bool readIdentity(HANDLE h, CDriveList::CDevice& drive) {
	memset(drive.identity, 0, sizeof(drive.identity));

	if (drive.scsiDirectFail) return false;	
	uint8_t* data = (uint8_t*)VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
	if (!data) return false;

	bool handleWasOpened = false;
	if (h == INVALID_HANDLE_VALUE) {
		h = CreateFile(drive.devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			if (data) VirtualFree(data, 0, MEM_RELEASE);
			return false;
		}
		handleWasOpened = true;
	}

	bool satl = false;
	if (drive.busType == CDriveList::StorageTypeBus::BusTypeAta || drive.busType == CDriveList::StorageTypeBus::BusTypeSata || drive.busType == CDriveList::StorageTypeBus::BusTypeAtapi) satl = drive.vendorId != L"ATA";

	bool ret = false;
	if (satl || drive.busType == CDriveList::StorageTypeBus::BusTypeScsi || drive.busType == CDriveList::StorageTypeBus::BusTypeUsb || drive.busType == CDriveList::StorageTypeBus::BusTypeRAID) {
		uint8_t cmd[16];
		memset(cmd, 0, sizeof(cmd));
		memset(data, 0, 512);
		cmd[0] = 0xa1; // SAT ATA PASSTHROUGH (12)
		cmd[1] = 4 << 1; // PIO data-in
		cmd[2] = 0x08 | 0x04 | 0x02; // dir = from device, 512 byte block, sector count = block cnt
		cmd[4] = 1; // block count
		cmd[9] = 0xec; // identity
		int v = doScsiIn(h, cmd, 12, data, 512, true);
		if (v > 0) ret = true;		
		else if (v < -1) drive.scsiDirectFail = true;
	}

	if (!ret) {
		if (drive.busType == CDriveList::StorageTypeBus::BusTypeUsb) ret = hdGetMetaHack(h, data, NULL, drive); else
		if (drive.busType == CDriveList::StorageTypeBus::BusTypeAta || drive.busType == CDriveList::StorageTypeBus::BusTypeSata || drive.busType == CDriveList::StorageTypeBus::BusTypeAtapi) ret = hdGetMetaATA(h, drive.busType == CDriveList::StorageTypeBus::BusTypeAtapi, data);
	}

	if (ret) {
		ataByteSwapIdentity(data);
		memcpy_s(drive.identity, 512, data, 512);
	}

	if (handleWasOpened && h != INVALID_HANDLE_VALUE) CloseHandle(h);
	if (data) VirtualFree(data, 0, MEM_RELEASE);

	return ret;
}


extern IPFS3* createPFS3FromVolume(AdfDevice* device, int partitionIndex, SectorCacheEngine* io, bool readOnly);

// Attempt to work out the file system and volume label
void identifyPartitionDrive(CDriveList::CDevice& drive, CDriveAccess* io) {
	if (!io) return;
	static bool adfSetup = false;
	if (!adfSetup) {
		adfSetup = true;
		adfPrepNativeDriver();
	}

	AdfDevice* adfDevice = adfDevOpenWithDriver(DISKFLASHBACK_AMIGA_DRIVER, (char*)io, AdfAccessMode::ADF_ACCESS_MODE_READONLY);
	if (!adfDevice) {
		delete io;
		return;
	}

	adfDevMount(adfDevice);

	drive.fileSystem = L"";
	drive.volumeName = L"";

	// See if theres any partitions
	for (size_t i = 0; i < adfDevice->nVol; i++) {

		AdfVolume* vol = adfVolMount(adfDevice, i, AdfAccessMode::ADF_ACCESS_MODE_READONLY);
		if (vol) {
			if (!drive.fileSystem.empty()) drive.fileSystem += L", ";
			drive.fileSystem += adfDosFsIsFFS(vol->fs.type) ? L"FFS" : L"OFS";
			std::wstring diskName;

			struct AdfRootBlock root;
			if (adfReadRootBlock(vol, (uint32_t)vol->rootBlock, &root) == ADF_RC_OK) {
				std::string tmp;
				tmp.resize(root.nameLen);
				if (root.nameLen) memcpy_s(&tmp[0], tmp.length(), root.diskName, root.nameLen);
				ansiToWide(tmp, diskName);
			}
			if (vol->volName) {
				std::wstring vn;
				ansiToWide(vol->volName, vn);
				if (diskName.empty()) diskName = vn; else diskName += L" (" + vn + L")";
			}
			if (diskName.length()) {
				if (!drive.volumeName.empty()) drive.volumeName += L", ";
				drive.volumeName += diskName;
			}

			adfVolUnMount(vol);
		}
		else {
			IPFS3* pfs3 = createPFS3FromVolume(adfDevice, i, io, true);
			if (pfs3) {
				std::wstring filesystemName;
				IPFS3::PFSVolInfo info;
				if (pfs3->GetVolInformation(info)) {
					switch (info.volType) {
					case IPFS3::DiskType::dt_beta: filesystemName = L"Beta PFS"; break;
					case IPFS3::DiskType::dt_pfs1: filesystemName = L"PFS"; break;
					case IPFS3::DiskType::dt_busy: filesystemName = L"Busy"; break;
					case IPFS3::DiskType::dt_muAF: filesystemName = L"muAF"; break;
					case IPFS3::DiskType::dt_muPFS: filesystemName = L"muPFS"; break;
					case IPFS3::DiskType::dt_afs1: filesystemName = L"AFS1"; break;
					case IPFS3::DiskType::dt_pfs2: filesystemName = L"PFS"; break;
					case IPFS3::DiskType::dt_pfs3: filesystemName = L"PFS"; break;
					case IPFS3::DiskType::dt_AFSU: filesystemName = L"AFSU"; break;
					default: break;
					}
					if (filesystemName.length()) {
						if (!drive.fileSystem.empty()) drive.fileSystem += L", ";
						drive.fileSystem += filesystemName;
					}

					if (info.volumeLabel.length()) {
						if (!drive.volumeName.empty()) drive.volumeName += L", ";
						std::wstring tmp;
						ansiToWide(info.volumeLabel, tmp);
						drive.volumeName += tmp;
					}
				}
				delete pfs3;
			}
		}
	}

	adfDevUnMount(adfDevice);
	adfDevClose(adfDevice);

	delete io;
}


void CDriveList::refreshList() {
	m_deviceList.clear();
	getDeviceList(m_deviceList);

	for (CDevice& dev : m_deviceList) 
		identifyPartitionDrive(dev, connectToDrive(dev, true));
}

CDriveList::CDriveList() {
}

int indexOfDrive(const std::wstring& deviceName, const CDriveList::DeviceList& deviceList) {
	for (size_t i = 0; i < deviceList.size(); i++)
		if (deviceList[i].deviceName == deviceName)
			return (int)i;
	return -1;
}

void makeDriveNameUnique(CDriveList::CDevice& drive, const CDriveList::DeviceList& deviceList) {
	const std::wstring baseName = drive.deviceName;
	drive.deviceName = L"";

	if (indexOfDrive(baseName, deviceList) < 0) {
		drive.deviceName = baseName;
		return;
	}

	uint32_t counter = 0;
	std::wstring newName;
	do {
		newName = baseName + L"_" + std::to_wstring(counter++);
	} while (indexOfDrive(newName, deviceList) >= 0);
	drive.deviceName = newName;
}

static bool doScsiRead10CHS(HANDLE handle, uint32_t lba, int c, int h, int s, uint8_t* data, int cnt, int* pflags, bool log) {
	uint8_t cmd[10];
	bool r;
	int flags = *pflags;

	memset(data, 0, sizeof(cmd));

	if (!flags) {
		// use direct ATA to read if direct ATA identity read succeeded
		if (hdGetMetaATA(handle, false, data)) flags = 2;
	}

	if (flags == 2) {
		memset(cmd, 0, sizeof(cmd));
		cmd[2] = cnt;
		cmd[3] = s;
		cmd[4] = c;
		cmd[5] = c >> 8;
		cmd[6] = 0xa0 | (h & 15);
		cmd[7] = 0x20; // read sectors
		r = hdReadATA(handle, cmd, data, cnt * 512);
		if (r) {
			*pflags = flags;
			return true;
		}
	}
	memset(data, 0, 512 * cnt);
	memset(cmd, 0, sizeof(cmd));

	cmd[0] = 0x28;
	if (lba != 0xffffffff) {
		cmd[2] = lba >> 24;
		cmd[3] = lba >> 16;
		cmd[4] = lba >> 8;
		cmd[5] = lba >> 0;
	}
	else {
		cmd[2] = h & 15;
		cmd[3] = c >> 8;
		cmd[4] = c;
		cmd[5] = s;
	}
	cmd[8] = cnt;
	return doScsiIn(handle, cmd, 10, data, 512 * cnt, false) > 0;
}

CDriveList::DangerType safetyCheck(HANDLE h, const std::wstring& name, uint64_t offset, uint8_t* buf, int blocksize, uint8_t* identity, bool canCHS) {
	uint64_t origoffset = offset;
	int i, blocks = 63;
	int specialaccessmode = 0;
	bool empty = true;

	for (int j = 0; j < blocks; j++) {
		memset(buf, 0xaa, blocksize);

		if (isCHS(identity) && canCHS) {
			int cc, hh, ss;
			toCHS(identity, j * 512, &cc, &hh, &ss);
			if (!doScsiRead10CHS(h, -1, cc, hh, ss, buf, 1, &specialaccessmode, false)) return CDriveList::DangerType::dtFailed;
		}
		else {
			LARGE_INTEGER fppos;
			fppos.QuadPart = offset;
			if (SetFilePointer(h, fppos.LowPart, &fppos.HighPart, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) return CDriveList::DangerType::dtFailed;
			DWORD outlen = 0;
			ReadFile(h, buf, blocksize, &outlen, NULL);
			if (outlen != blocksize) return CDriveList::DangerType::dtFailed;
		}

		if (j == 0 && offset > 0) return CDriveList::DangerType::dtPartition;
		if (j == 0 && buf[0] == 0x39 && buf[1] == 0x10 && buf[2] == 0xd3 && buf[3] == 0x12) return CDriveList::DangerType::dtCPRM;
		if (!memcmp(buf, "RDSK", 4) || !memcmp(buf, "DRKS", 4)) return CDriveList::DangerType::dtRDB;
		if (!memcmp(buf + 2, "CIS@", 4) && !memcmp(buf + 16, CA, strlen(CA))) return CDriveList::DangerType::dtSRAM;
		if (j == 0) 
			for (i = 0; i < blocksize; i++) 
				if (buf[i]) empty = false;
		offset += blocksize;
	}
	if (!empty) {
		int mounted = isMounted(h);
		if (!mounted) return CDriveList::DangerType::dtUnknown;
		if (mounted < 0) return CDriveList::DangerType::dtOS;
		return CDriveList::DangerType::dtMBR;
	}
	return CDriveList::DangerType::dtEmptySpace;
}

void getDevicePropertyFromName(const std::wstring& devicePath, bool ignoreDuplicates, CDriveList::DeviceList& deviceList) {
	CDriveList::CDevice _device;
	CDriveList::CDevice& device = _device;
	device.devicePath = devicePath;

	HANDLE hDevice = CreateFile(device.devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE) hDevice = CreateFile(device.devicePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE) {
		hDevice = CreateFile(device.devicePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hDevice == INVALID_HANDLE_VALUE) return;
		device.isAccessible = false;
	}

	std::vector<uint8_t> tmpBuffer; tmpBuffer.resize(20000);

	int driveIndex = -1;
	DWORD returnedLength;
	STORAGE_PROPERTY_QUERY query;
	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;		
	if (!DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(STORAGE_PROPERTY_QUERY), &tmpBuffer[0], (DWORD)tmpBuffer.size(), &returnedLength, NULL)) {
		DWORD err = GetLastError();
		if (err != ERROR_INVALID_FUNCTION) {
			CloseHandle(hDevice);
			return;
		}
		device.isRemovable = true;
		device.deviceName = device.devicePath;
		device.productId = L"Disk";
		device.productRev = L"1.0";
		device.vendorId = L"Unknown";
	}
	else {
		driveIndex = getStorageProperty(&tmpBuffer[0], returnedLength, device, ignoreDuplicates, deviceList);
		if (driveIndex == DEVICE_INDEX_IGNORE) {
			CloseHandle(hDevice);
			return;
		}
		if (driveIndex != DEVICE_INDEX_ADD) device = deviceList[driveIndex];
		STORAGE_DEVICE_NUMBER sdn;		
		if (DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &returnedLength, NULL)) getStorageInfo(device, sdn);
		readIdentity(INVALID_HANDLE_VALUE, device);
	}
	
	bool geometryOK = true;
	DISK_GEOMETRY dg;
	if (!DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, (void*)&dg, sizeof(dg), &returnedLength, NULL)) {
		DWORD err = GetLastError();
		if (ISNOMEDIAERROR(err)) {
			CloseHandle(hDevice);
			return;
		}
		geometryOK = false;
	}
	if (!DeviceIoControl(hDevice, IOCTL_DISK_IS_WRITABLE, NULL, 0, NULL, 0, &returnedLength, NULL)) {
		DWORD err = GetLastError();
		if (err == ERROR_WRITE_PROTECT) device.readOnly = true;
	}

	if (!device.isAccessible) {
		device.danger = CDriveList::DangerType::dtBad;
		device.readOnly = true;
		makeDriveNameUnique(device, deviceList);
	}
	else {
		bool getDiskLengthOK = true;
		GET_LENGTH_INFORMATION gli;
		gli.Length.QuadPart = 0;
		if (!DeviceIoControl(hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, (void*)&gli, sizeof(gli), &returnedLength, NULL)) getDiskLengthOK = false;
		if (!geometryOK && !getDiskLengthOK) {
			CloseHandle(hDevice);
			return;
		}

		device.offset = 0;
		device.size = 0;
		if (geometryOK) {
			device.bytesPerSector = dg.BytesPerSector;
			if ((dg.BytesPerSector < 512) || (dg.BytesPerSector > 2048)) {
				CloseHandle(hDevice);
				return;
			}
			device.size = (uint64_t)dg.BytesPerSector * (uint64_t)dg.Cylinders.QuadPart * (uint64_t)dg.TracksPerCylinder * (uint64_t)dg.SectorsPerTrack;
			device.cylinders = dg.Cylinders.QuadPart > 65535 ? 0 : dg.Cylinders.LowPart;
			device.sectorsPerTrack = dg.SectorsPerTrack;
			device.heads = dg.TracksPerCylinder;
		}

		if (getDiskLengthOK && gli.Length.QuadPart) device.size = gli.Length.QuadPart;

		// CHS stands for Cylinder/Head/Sector
		if (isCHS(device.identity) && gli.Length.QuadPart == 0) {
			int c, h, s;
			toCHS(device.identity, -1, &c, &h, &s);
			device.size = (uint64_t)c * h * s * 512;
			device.cylinders = c;
			device.heads = h;
			device.sectorsPerTrack = s;
			device.chsDetected = true;
		}

		uint32_t partitionsFound = 0;
		memset(&tmpBuffer[0], 0, tmpBuffer.size());
		if (DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT, NULL, 0, &tmpBuffer[0], (DWORD)tmpBuffer.size(), &returnedLength, NULL)) {
			const DRIVE_LAYOUT_INFORMATION* dli = (DRIVE_LAYOUT_INFORMATION*)&tmpBuffer[0];
			if (dli->PartitionCount) {
				int nonzeropart = 0;
				int gotpart = 0;
				int safepart = 0;
				for (DWORD i = 0; i < dli->PartitionCount; i++) {
					const PARTITION_INFORMATION* pi = &dli->PartitionEntry[i];
					if (pi->PartitionType == PARTITION_ENTRY_UNUSED) continue;
					if (pi->RecognizedPartition == 0) continue;
					nonzeropart++;
					if (pi->PartitionType != 0x76 && pi->PartitionType != 0x30) continue;
					CDriveList::CDevice dev = device;
					dev.offset = pi->StartingOffset.QuadPart;
					dev.size = pi->PartitionLength.QuadPart;
					makeDriveNameUnique(dev, deviceList);
					dev.danger = CDriveList::DangerType::dtPartition;
					dev.partitionNumber = (uint16_t)(i+1);					
					deviceList.push_back(dev);
					partitionsFound++;
				}
			}
		}
		if (partitionsFound < 1) {
			if (device.offset == 0 && device.size) {
				device.danger = safetyCheck(hDevice, device.devicePath, 0, &tmpBuffer[0], dg.BytesPerSector, device.identity, device.chsDetected);
				if (device.danger >= CDriveList::DangerType::dtOS) {
					CloseHandle(hDevice);
					return;
				}
			}
			if (driveIndex < 0) deviceList.push_back(device);
			makeDriveNameUnique(device, deviceList);
		}
	}	
	CloseHandle(hDevice);
}

bool getDeviceProperty(HDEVINFO devInfo, DWORD Index, CDriveList::DeviceList& deviceList) {
	SP_DEVICE_INTERFACE_DATA            interfaceData;
	PSP_DEVICE_INTERFACE_DETAIL_DATA    interfaceDetailData = NULL;
	BOOL                                status;
	DWORD								reqSize, errorCode;

	interfaceData.cbSize = sizeof(SP_INTERFACE_DEVICE_DATA);
	status = SetupDiEnumDeviceInterfaces(devInfo, 0, &GUID_DEVINTERFACE_DISK, Index, &interfaceData);
	if (status == FALSE) return false;

	status = SetupDiGetDeviceInterfaceDetail(devInfo, &interfaceData, NULL, 0, &reqSize, NULL);
	if (status == FALSE) {
		errorCode = GetLastError();
		if (errorCode != ERROR_INSUFFICIENT_BUFFER) return false;
	}

	interfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(reqSize);
	if (!interfaceDetailData) return false;

	interfaceDetailData->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);
	DWORD inputSize = reqSize;
	status = SetupDiGetDeviceInterfaceDetail(devInfo, &interfaceData, interfaceDetailData, inputSize, &reqSize, NULL);
	if (status == FALSE) {
		free(interfaceDetailData);
		return false;
	}

	getDevicePropertyFromName(interfaceDetailData->DevicePath, false, deviceList);
	free(interfaceDetailData);
	return true;
}

std::vector<CDriveList::CDevice> CDriveList::getDevices() {
	return m_deviceList;
}


// Based on code from hardfile_win32.cpp in the WinUAE source code
void CDriveList::getDeviceList(std::vector<CDevice>& deviceList) {
	HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
	if (hDevInfo != INVALID_HANDLE_VALUE) {
		DWORD index = 0;
		while (getDeviceProperty(hDevInfo, index++, m_deviceList));
		SetupDiDestroyDeviceInfoList(hDevInfo);
	}

	// This isn't stricly required as it would get rejected later on, but always best to be safe
	WCHAR badDrive[MAX_PATH];
	if (GetWindowsDirectory(badDrive, MAX_PATH) > 0) CharUpper(badDrive); else {
		// If for some crazy reason the above fails, then look where the application is running from
		GetModuleFileName(NULL, badDrive, MAX_PATH);
		if (GetLastError() != ERROR_SUCCESS) badDrive[0] = L'\0'; else CharUpper(badDrive);
	}
	
	WCHAR tmp[20];
	DWORD dwDriveMask = GetLogicalDrives();
	for (WCHAR drive = L'A'; drive <= L'Z'; drive++) {
		if ((dwDriveMask & 1) && (drive != badDrive[0])) {
			swprintf_s(tmp, L"%c:\\", drive);
			const DWORD driveType = GetDriveType(tmp);
			if ((driveType == DRIVE_FIXED) || (driveType == DRIVE_REMOVABLE)) {
				swprintf_s(tmp, L"\\\\.\\%c:", drive);
				getDevicePropertyFromName(tmp, true, m_deviceList);
			}
		}
		dwDriveMask >>= 1;
	}
}


// Connect to a specific drive
CDriveAccess* CDriveList::connectToDrive(const CDevice& device, bool forceReadOnly) {
	CDriveAccess* ret = new CDriveAccess();
	if (ret->openDrive(device, forceReadOnly)) return ret;
	delete ret;
	return nullptr;
}

CDriveAccess::CDriveAccess() : SectorCacheEngine(512*40), m_drive(INVALID_HANDLE_VALUE) {
}

CDriveAccess::~CDriveAccess() {
	closeDrive();
}

void CDriveAccess::closeDrive() {
	for (HANDLE h : m_lockedVolumes) CloseHandle(h);
	m_lockedVolumes.clear();
	if (m_dontFreeHandle) return;
	if (m_drive != INVALID_HANDLE_VALUE) {
		CloseHandle(m_drive);
		m_drive = INVALID_HANDLE_VALUE;
	}
}

bool CDriveAccess::openDrive(const CDriveList::CDevice& device, bool readOnly) {
	closeDrive();
	m_device = device;

	DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS;
	m_device.readOnly |= readOnly;
	
	m_drive = CreateFile(device.devicePath.c_str(), GENERIC_READ | (m_device.readOnly && !device.chsDetected ? 0 : GENERIC_WRITE), FILE_SHARE_READ | (m_device.readOnly && !device.chsDetected ? 0 : FILE_SHARE_WRITE), NULL, OPEN_EXISTING, flags, NULL);
	if (m_drive == INVALID_HANDLE_VALUE && !m_device.readOnly) {
		DWORD err = GetLastError();		
		if (err == ERROR_WRITE_PROTECT || err == ERROR_SHARING_VIOLATION) {
			m_drive = CreateFile(m_device.devicePath.c_str(), GENERIC_READ,FILE_SHARE_READ,NULL, OPEN_EXISTING, flags, NULL);
			if (m_drive != INVALID_HANDLE_VALUE) m_device.readOnly = true;
		}
	}

	if (m_drive == INVALID_HANDLE_VALUE) return false;

	if (m_device.offset == 0) {
		std::vector<uint8_t> buffer;
		buffer.resize(m_device.bytesPerSector);
		CDriveList::DangerType danger = safetyCheck(m_drive, m_device.devicePath.c_str(), 0, &buffer[0], m_device.bytesPerSector, m_device.identity, m_device.chsDetected);
		if (danger >= CDriveList::DangerType::dtOS) {
			CloseHandle(m_drive);
			return false;
		}
	}

	// Next up, attempt to lock the drive
	DWORD written;
	if (DeviceIoControl(m_drive, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &written, NULL)) {
		if (DeviceIoControl(m_drive, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &written, NULL)) {
			m_dismounted = true;
			return true;
		}
	}

	DWORD signature, partitionStyle;
	if (!getSignatureFromHandle(m_drive, signature, partitionStyle)) return true;	
	
	bool ntfs_found = false;
	WCHAR volName[MAX_PATH];
	HANDLE h = FindFirstVolume(volName, MAX_PATH);
	while (h != INVALID_HANDLE_VALUE) {
		bool isntfs = false;
		if (volName[wcslen(volName) - 1] == '\\') volName[wcslen(volName) - 1] = L'\0';

		HANDLE d = CreateFile(volName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (d != INVALID_HANDLE_VALUE) {
			if (DeviceIoControl(d, FSCTL_IS_VOLUME_MOUNTED, NULL, 0, NULL, 0, &written, NULL)) {
				NTFS_VOLUME_DATA_BUFFER ntfs;
				if (DeviceIoControl(d, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &ntfs, sizeof ntfs, &written, NULL)) isntfs = true;
				DWORD outsize = sizeof(VOLUME_DISK_EXTENTS) + sizeof(DISK_EXTENT) * 32;

				VOLUME_DISK_EXTENTS* vde = (VOLUME_DISK_EXTENTS*)malloc(outsize);
				if (vde) {
					if (DeviceIoControl(d, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde, outsize, &written, NULL)) {
						for (DWORD i = 0; i < vde->NumberOfDiskExtents; i++) {
							bool driveFound = false;
							WCHAR pdrv[MAX_PATH];
							swprintf_s(pdrv, L"\\\\.\\PhysicalDrive%d", vde->Extents[i].DiskNumber);
							HANDLE ph = CreateFile(pdrv, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
							if (ph != INVALID_HANDLE_VALUE) {
								DWORD sign2, pstyle2;
								if (getSignatureFromHandle(ph, sign2, pstyle2)) {
									if (signature == sign2 && pstyle2 == PARTITION_STYLE_MBR) {
										if (!isntfs) {
											m_lockedVolumes.push_back(d);
											d = INVALID_HANDLE_VALUE;
										} else ntfs_found = true;
									}
								}
								CloseHandle(ph);
							}
						}
					}
				}
				free(vde);
			}
			if (d != INVALID_HANDLE_VALUE) CloseHandle(d);
		}
		if (!FindNextVolume(h, volName, MAX_PATH)) break;
	}
	FindVolumeClose(h);

	if (ntfs_found) {
		for (HANDLE h : m_lockedVolumes) CloseHandle(h);
		m_lockedVolumes.clear();
	}
	else {
		for (HANDLE d : m_lockedVolumes) 
			if (DeviceIoControl(d, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &written, NULL))
				DeviceIoControl(d, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &written, NULL);
	}
	
	return true;
}

bool CDriveAccess::seek(uint64_t offset) {
	if (m_drive == INVALID_HANDLE_VALUE) return false;
	DWORD ret;

	if (m_device.size) {
		if (offset >= m_device.size) return false;
		offset += m_device.offset;
		// Offset must be multiple of sector size
		if (offset & (m_device.bytesPerSector - 1)) return false;
	}

	LARGE_INTEGER fppos;
	fppos.QuadPart = offset;
	ret = SetFilePointer(m_drive, fppos.LowPart, &fppos.HighPart, FILE_BEGIN);
	if (ret == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) return false;

	return true;
}

// Data must be at least getSectorSize() size
bool CDriveAccess::internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
	if (!seek(sectorNumber * sectorSize)) return false;
	DWORD bytesRead = 0;
	if (!ReadFile(m_drive, data, sectorSize, &bytesRead, NULL)) bytesRead = 0;
	return bytesRead == sectorSize;
}

bool CDriveAccess::internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
	if (m_device.readOnly) return false;
	if (!seek(sectorNumber * sectorSize)) return false;

	DWORD bytesWritten = 0;
	if (!WriteFile(m_drive, data, sectorSize, &bytesWritten, NULL)) bytesWritten = 0;
	return bytesWritten == sectorSize;
}


bool CDriveAccess::isDiskPresent() {
	return m_drive != INVALID_HANDLE_VALUE;
}

bool CDriveAccess::isDiskWriteProtected() {
	return m_device.readOnly;
}

// Total number of tracks avalable
uint32_t CDriveAccess::totalNumTracks() {
	return m_device.cylinders;
}

uint32_t CDriveAccess::getNumHeads() {
	return m_device.heads;
}

uint64_t CDriveAccess::getDiskDataSize() {
	return m_device.size;
}

std::wstring CDriveAccess::getDriverName() {
	return m_device.deviceName;
}

uint32_t CDriveAccess::sectorSize() {
	return m_device.bytesPerSector;
}

uint32_t CDriveAccess::numSectorsPerTrack() {
	return m_device.sectorsPerTrack;
}

SectorType CDriveAccess::getSystemType() {
	return SectorType::stAmiga;
}

uint32_t CDriveAccess::id() {
	return _wtoi(m_device.productId.c_str());
}

uint32_t CDriveAccess::serialNumber() {
	return _wtoi(m_device.productSerial.c_str());
}

bool CDriveAccess::available() {
	return m_drive != INVALID_HANDLE_VALUE;
}

void CDriveAccess::quickClose() {
	closeDrive();
}