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
#include "sectorCache.h"
#include <vector>
#include <string>

class CDriveAccess;

// Class for enumerating and accessing physical drives as safely as possible
class CDriveList {
public:
	enum class DangerType : int8_t { dtBad=-10, dtEmptySpace=-9, dtUnknown = -8, dtMBR = -6, dtPartition = -5, dtCPRM = -3, dtSRAM = -2, dtRDB = -1, dtOS=0, dtFailed=1 };

	enum class StorageTypeBus {
		BusTypeUnknown = 0x00,
		BusTypeScsi,
		BusTypeAtapi,
		BusTypeAta,
		BusType1394,
		BusTypeSsa,
		BusTypeFibre,
		BusTypeUsb,
		BusTypeRAID,
		BusTypeiScsi,
		BusTypeSas,
		BusTypeSata,
		BusTypeSd,
		BusTypeMmc,
		BusTypeVirtual,
		BusTypeFileBackedVirtual,
		BusTypeSpaces,
		BusTypeNvme,
		BusTypeSCM,
		BusTypeUfs,
		BusTypeMax,
		BusTypeMaxReserved = 0x7F
	};

	struct CDevice {
		std::wstring devicePath;
		bool isAccessible = true;
		bool isRemovable = false;

		DangerType danger = DangerType::dtBad;

		bool readOnly = false;
		bool scsiDirectFail = false;
		bool chsDetected = false;
		uint16_t partitionNumber = 0;  // 0 if not a partition

		uint32_t bytesPerSector = 512;
		uint64_t size = 0;
		uint64_t offset = 0;
		uint32_t cylinders = 0;
		uint32_t sectorsPerTrack = 0;
		uint32_t heads = 0;

		StorageTypeBus busType = StorageTypeBus::BusTypeUnknown;

		uint8_t identity[512];

		uint32_t usbVID = 0;
		uint32_t usbPID = 0;
		std::wstring vendorId;
		std::wstring productId;
		std::wstring productRev;
		std::wstring productSerial;
		std::wstring deviceName;
	};
	typedef std::vector<CDevice> DeviceList;
private:
	DeviceList m_deviceList;
	// Get a list of devices
	void getDeviceList(std::vector<CDevice>& deviceList);

public:
	// Constructor
	CDriveList();

	void refreshList();

	std::vector<CDevice> getDevices();

	// Connect to a specific drive
	CDriveAccess* connectToDrive(const CDevice& device, bool forceReadOnly);
};

// Class for access to a physical drive
class CDriveAccess : public SectorCacheEngine {
	friend class CDriveList;
private:
	CDriveList::CDevice m_device;
	HANDLE m_drive;
	std::vector<HANDLE> m_lockedVolumes;
	bool m_dismounted = false;

	bool openDrive(const CDriveList::CDevice& device, bool readOnly);
	bool seek(uint64_t offset);

	virtual bool internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) override;
	virtual bool internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) override;

public:
	CDriveAccess();
	~CDriveAccess();

	uint32_t getSectorSize() const { return m_device.bytesPerSector; };

	virtual bool isDiskPresent() override;
	virtual bool isDiskWriteProtected() override;
	virtual uint32_t totalNumTracks() override;
	virtual uint64_t getDiskDataSize() override;
	virtual uint32_t getNumHeads() override;
	virtual std::wstring getDriverName() override;
	virtual uint32_t sectorSize() override;
	virtual bool isPhysicalDisk() { return false; };
	virtual bool allowCopyToFile() { return false; };
	virtual uint32_t numSectorsPerTrack() override;
	virtual SectorType getSystemType() override;
	virtual uint32_t id() override;
	virtual uint32_t serialNumber() override;
	virtual bool available() override;
	virtual void quickClose() override;
	void closeDrive();
	
};