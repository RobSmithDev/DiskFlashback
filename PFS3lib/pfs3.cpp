
// Simple PFS3 Wrapper with Little-Big Endian conversion, by Robert Smith (@RobSmithDev)
// This product includes software developed by Michiel Pelt.

#include "pfs3.h"
#include "blocks.h"
#include "struct.h"
#include "exec_nodes.h"
#include "disk.h"
#include "kswrapper.h"
#include "support.h"
#include "volume.h"
#include "init.h"
#include "directory.h"
#include "lock.h"
#include "lru.h"
#include "update.h"
#include <stdarg.h>


class PFS3 : public IPFS3 {
private:
	const std::function<bool(uint32_t physicalSector, uint32_t readSize, void* data)> m_readSector;
	const std::function<bool(uint32_t physicalSector, uint32_t readSize, const void* data)> m_writeSector;
	const std::function<void(const std::string& message)> m_pfsError;
	bool m_readOnly;

	globaldata* g = nullptr;

	// Works out obj for the specified path - must end in a / or '' for root
	PFS3::Error findObjectInfoForPath(const std::string& path, objectinfo& obj, std::string& leftOver);
public:

	const uint8_t MaxNameLength();

	PFS3(const struct DriveInfo& drive, const struct PartitionInfo& partition,
		const std::function<bool(uint32_t physicalSector, uint32_t readSize, void* data)> readSector,
		const std::function<bool(uint32_t physicalSector, uint32_t readSize, const void* data)> writeSector,
		const std::function<void(const std::string& message)> pfsError,
		const bool readOnly);
	~PFS3();

	// Is it available?
	virtual bool available() override;

	// Can return: eNotFound, eNotDirectory, eUnknown, eOK, eWriteProtected, eAlreadyExists, eDiskFull, eBadName
	virtual Error MkDir(const std::string& path) override;
	// Can return: eNotFound, eNotDirectory, eUnknown, eOK, eWriteProtected, eNotFound, eNotEmpty
	virtual Error RmDir(const std::string& path) override;
	// Can return: eNotFound, eNotDirectory, eUnknown, eOK, eWriteProtected, eNotFound
	virtual Error Deletefile(const std::string& path) override;

	// Can return: eNotFound, eNotDirectory, eUnknown, eOK, eWriteProtected, eDiskFull, eAlreadyExists, eInUse
	virtual Error Movefile(const std::string& originalName, const std::string& newName, const bool replaceExisting) override;

	// Get details about a volume
	virtual bool GetVolInformation(struct PFSVolInfo& volInfo) override;

	// Get details about a file
	virtual Error GetFileInformation(const std::string& path, FileInformation& fileDir) override;

	// Create/open file:   eNotFile, eAccessDenied
	virtual Error Createfile(const std::string& filename, OpenMode openMode, bool readOnly, PFS3File& file) override;

	// Seek to a position into the file.  Errors: eUnknown, eNotFile, eWriteProtected, eDiskFull, eFailed
	virtual Error Seekfile(PFS3File& file, uint64_t offset, SeekMode mode) override;

	// Get the current file cursor position
	virtual Error GetfilePos(PFS3File& file, uint64_t& position) override;

	// Truncate file to current position
	virtual Error ChangefileSize(PFS3File& file, uint64_t newSize) override;

	// Truncate file to current position
	virtual Error GetfileSize(PFS3File& file, uint64_t& size) override;

	// Truncate to the current file position
	virtual Error Truncatefile(PFS3File& file) override;

	// Flush changes
	virtual Error Flushfile(PFS3File& file) override;

	// Change file attributes - eNotFound, eNotFile, eUnknown
	virtual Error SetfileAttributes(const std::string& filename, uint32_t newAttributes) override;

	// Change file date - eNotFound, eNotFile, eUnknown
	virtual Error SetfileDate(const std::string& filename, const struct tm& stamp) override;

	// Read from a file
	virtual Error Readfile(PFS3File& file, void* data, uint32_t dataSize, uint32_t& dataRead) override;

	// Write to a file
	virtual Error Writefile(PFS3File& file, const void* data, uint32_t dataSize, uint32_t& dataWritten) override;;

	// Close the file
	virtual void Closefile(PFS3File& file) override;

	// Flush changes to disk
	virtual void flushChanges(bool force) override;

	// Scan a directory with callback for files/dirs. ** Can return: eNotFound, eNotDirectory, eUnknown, eOK
	virtual Error Dir(const std::string& path, std::function<void(const FileInformation& fileDir)> onFileDir) override;
};

IPFS3* IPFS3::createInstance(const struct DriveInfo& drive, const struct PartitionInfo& partition, const std::function<bool(uint32_t physicalSector, uint32_t readSize, void* data)> readSector, const std::function<bool(uint32_t physicalSector, uint32_t readSize, const void* data)> writeSector, const std::function<void(const std::string& message)> pfsError, const bool readOnly) {
	return new PFS3(drive, partition, readSector, writeSector, pfsError, readOnly);
}



LONG _NormalErrorMsg(CONST_STRPTR melding, APTR arg, ULONG notargets, globaldata* g) {
	std::string output;

	ULONG* args = (ULONG*)arg;
	if (args) {
		char tmp[1024];
		// A bit hacky but works for the messages used by PFS3
		int argNum = 0;

		const char* src = melding;
		const char* found = strchr(src, '%');
		while (found) {
			// Grab the message before
			strncpy_s(tmp, src, found - src);
			tmp[found - src] = '\0';
			output += tmp;

			if ((found[1] == '0') && (found[2] == '8') && (found[3] == 'l') && ((found[4] == 'x') || (found[4] == 'X'))) {
				src = found + 5;
				sprintf_s(tmp,1024, "%08lx", args[argNum++]);
				output += tmp;
			} else 
				if (found[1] == 'l') {
					if (found[2] == 'u') {
						src = found + 3;
						sprintf_s(tmp, 1024, "%lu", args[argNum++]);
						output += tmp;
					}
					else if (found[2] == 'd') {
						src = found + 3;
						sprintf_s(tmp, 1024, "%ld", args[argNum++]);
						output += tmp;
					}
					else src++;
				}
			found = strchr(src, '%');
		}
		// If theres any left we append it
		output += src;
	}
	else output = melding;

	g->handleError(output);
}



PFS3::PFS3(const struct PFS3::DriveInfo& drive, const struct PartitionInfo& partition, 
		const std::function<bool(uint32_t physicalSector, uint32_t readSize, void* data) > readSector,
		const std::function<bool(uint32_t physicalSector, uint32_t readSize, const void* data) > writeSector,
		const std::function<void(const std::string& message)> pfsError,
		const bool readOnly
	)
	: m_readSector(readSector), m_writeSector(writeSector), m_pfsError(pfsError), IPFS3(), m_readOnly(readOnly) {

	g = (globaldata*)malloc(sizeof(globaldata));
	if (!g) return;
	memset(g, 0, sizeof(globaldata));

	g->softprotect = readOnly ? 1 : 0;
	g->dosenvec.de_SizeBlock = partition.blockSize;
	g->dosenvec.de_SectorPerBlock = partition.sectorsPerBlock;
	g->dosenvec.de_BlocksPerTrack = partition.blocksPerTrack;
	g->dosenvec.de_LowCyl = partition.lowCyl;
	g->dosenvec.de_HighCyl = partition.highCyl;
	g->dosenvec.de_Mask = partition.mask;
	g->dosenvec.de_NumBuffers = partition.numBuffer;
	g->geom.dg_CylSectors = drive.cylBlocks;
	g->geom.dg_SectorSize = drive.sectorSize;
	g->geom.dg_Cylinders = drive.numCylinders;
	g->geom.dg_TotalSectors = g->geom.dg_Cylinders * g->geom.dg_CylSectors;
	g->geom.dg_Heads = drive.numHeads;
	g->geom.dg_TrackSectors = g->geom.dg_TotalSectors;
	g->geom.dg_BufMemType = g->geom.dg_BufMemType;
	g->geom.dg_DeviceType = g->geom.dg_DeviceType;
	g->geom.dg_Flags = g->geom.dg_Flags;
	g->ErrorMsg = _NormalErrorMsg;

	g->readSector = [this](uint32_t logicalSector, void* data) {
		// TODO: Handle g->blocksize not being 512
		if (!m_readSector) return false;
		return m_readSector(g->firstblocknative + logicalSector, g->blocksize, data);
	};

	g->writeSector = [this](uint32_t logicalSector, void* data) {
		if (m_readOnly) return false;
		// TODO: Handle g->blocksize not being 512
		if (!m_writeSector) return false;
		return m_writeSector(g->firstblocknative + logicalSector, g->blocksize, data);
	};

	g->handleError = m_pfsError;

	g->largeDiskSafeOS = true;

	g->currentvolume = NULL;
	CalculateBlockSize(g, 0, 0);	
	g->harddiskmode = true;

	InitDataCache(g);

	if (GetCurrentRoot(&g->rootblock, g)) {
		DiskInsertSequence(g->rootblock, g);
	}
	else {
		free(g);
		g = nullptr;
	}
}

// Is it available?
bool PFS3::available() {
	return g != nullptr;
}


void PFS3::flushChanges(bool force) {
	if (g->dirty) {
		if (force) UpdateDisk(g); else CheckUpdate(RTBF_POSTPONED_TH, g);
		//FreeUnusedResources(g->currentvolume, g);
	}
}


// Works out obj for the specified path - must end in a / or '' for root
PFS3::Error PFS3::findObjectInfoForPath(const std::string& path, objectinfo& obj, std::string& leftOver) {
	leftOver = "";
	memset(&obj, 0, sizeof(obj));
	SIPTR er = 0;
	char* filenamePath;
	if (path == "/") filenamePath = (char*)GetFullPath(NULL, (STRPTR)"", &obj, &er, g); else
		filenamePath = (char*)GetFullPath(NULL, (STRPTR)path.c_str(), &obj, &er, g);

	if (!filenamePath) {
		if (er == PFS_ERROR_OBJECT_NOT_FOUND) return PFS3::Error::eNotFound;
		if (er == PFS_ERROR_OBJECT_WRONG_TYPE) return PFS3::Error::eNotDirectory;
		return PFS3::Error::eUnknown;
	}
	leftOver = filenamePath;

	return PFS3::Error::eOK;
}


PFS3::Error PFS3::MkDir(const std::string& path) {
	std::string leftOver;
	objectinfo obj;

	if (path.empty()) return Error::eUnknown;
	Error err = findObjectInfoForPath(path, obj, leftOver);
	if (leftOver.empty()) return Error::eAlreadyExists;
	if (err != Error::eOK) return err;

	SIPTR error = 0;
	if (NewDir(&obj, (char*)leftOver.c_str(), &error, g)) {
		return Error::eOK;
	}
	else {
		switch (error) {
		case PFS_ERROR_WRITE_PROTECTED: return Error::eAccessDenied;
		case PFS_ERROR_DISK_FULL: return Error::eDiskFull;
		case PFS_ERROR_INVALID_COMPONENT_NAME: return Error::eBadName;
		case PFS_ERROR_OBJECT_EXISTS: return Error::eAlreadyExists;
		case PFS_ERROR_DISK_WRITE_PROTECTED: return Error::eWriteProtected;
		default: return Error::eUnknown;
		}
	}
	return Error::eOK;
}

PFS3::Error PFS3::RmDir(const std::string& path) {
	std::string leftOver;
	objectinfo obj;

	if (path.empty()) return Error::eUnknown;
	std::string thePath = path;
	if (thePath[thePath.length() - 1] != '/') thePath += "/";
	Error err = findObjectInfoForPath(thePath, obj, leftOver);
	if (err != Error::eOK) return err;
	if (!leftOver.empty()) return Error::eNotFound;

	SIPTR error;
	if (DeleteObject(&obj, &error, g)) {
		//flushChanges(true);
		return Error::eOK;
	}
	else {
		switch (error) {
		case PFS_ERROR_DIRECTORY_NOT_EMPTY: return Error::eNotEmpty;
		case PFS_ERROR_DISK_WRITE_PROTECTED: return Error::eWriteProtected;
		case PFS_ERROR_WRITE_PROTECTED: return Error::eAccessDenied;
		default: return Error::eUnknown;
		}
	}
}


PFS3::Error PFS3::Deletefile(const std::string& path) {
	std::string leftOver;
	objectinfo obj, srcFile;

	if (path.empty()) return Error::eUnknown;
	Error err = findObjectInfoForPath(path, obj, leftOver);
	if (err != Error::eOK) return err;
	if (leftOver.empty()) return Error::eNotFound;

	SIPTR error = 0;
	memset(&srcFile, 0, sizeof(srcFile));
	if (!FindObject(&obj, (char*)leftOver.c_str(), &srcFile, &error, g)) {
		if (error == PFS_ERROR_OBJECT_NOT_FOUND) return Error::eNotFound;
		return Error::eUnknown;
	}

	if (DeleteObject(&srcFile, &error, g)) {
		//flushChanges(true);
		return Error::eOK;
	}
	else {
		switch (error) {
		case PFS_ERROR_DIRECTORY_NOT_EMPTY: return Error::eNotEmpty;
		case PFS_ERROR_DISK_WRITE_PROTECTED: return Error::eWriteProtected;
		case PFS_ERROR_WRITE_PROTECTED: return Error::eAccessDenied;
		default: return Error::eUnknown;
		}
	}
}

//  Can return: 
PFS3::Error PFS3::Movefile(const std::string& originalName, const std::string& newName, const bool replaceExisting) {
	objectinfo srcObj, srcFile, dstFolder;
	std::string leftOver;
	Error err = findObjectInfoForPath(originalName, srcObj, leftOver);
	if (err != Error::eOK) return err;

	SIPTR error = 0;
	if (!FindObject(&srcObj, (char*)leftOver.c_str(), &srcFile, &error, g)) {		
		if (error == PFS_ERROR_OBJECT_NOT_FOUND) return Error::eNotFound;
		if (error == PFS_ERROR_OBJECT_WRONG_TYPE) return Error::eNotDirectory;
		return Error::eUnknown;
	}

	err = findObjectInfoForPath(newName, dstFolder, leftOver);
	if (err != Error::eOK) return err;

	if (replaceExisting) {
		objectinfo dstFile;
		memset(&dstFile, 0, sizeof(dstFile));
		// Delete it if it exists
		if (FindObject(&dstFolder, (char*)leftOver.c_str(), &dstFile, &error, g)) {
			err = Deletefile(newName);
			if (err != Error::eOK) return err;
		}
	}

	if (!RenameAndMove(&srcObj, &srcFile, &dstFolder, (char*)leftOver.c_str(), &error, g)) {
		switch (error) {
		case PFS_ERROR_OBJECT_NOT_FOUND: return Error::eNotFound;
		case PFS_ERROR_WRITE_PROTECTED: return Error::eAccessDenied;
		case PFS_ERROR_DISK_WRITE_PROTECTED: return Error::eWriteProtected;
		case PFS_ERROR_DISK_FULL: return Error::eDiskFull;
		case PFS_ERROR_OBJECT_EXISTS: return Error::eAlreadyExists;
		case PFS_ERROR_OBJECT_IN_USE: return Error::eInUse;
		default: return Error::eUnknown;
		}
	}
	return Error::eOK;
}


bool PFS3::GetVolInformation(struct PFSVolInfo& volInfo) {
	if (!g) return false;
	if (!g->currentvolume) return false;

	volInfo.readOnly = g->softprotect;
	volInfo.totalBlocks = g->currentvolume->numblocks - g->rootblock->lastreserved - g->rootblock->alwaysfree - 1;
	volInfo.blocksUsed = volInfo.totalBlocks - g->glob_allocdata.alloc_available;
	volInfo.bytesPerBlock = g->currentvolume->bytesperblock;
	volInfo.volumeLabel.resize(g->rootblock->diskname[0]);

	switch (g->disktype) {
	case 0x42455441L: volInfo.volType = DiskType::dt_beta; break;
	case ID_PFS_DISK:volInfo.volType = DiskType::dt_pfs1; break;
	case ID_BUSY:volInfo.volType = DiskType::dt_busy; break;
	case ID_MUAF_DISK:volInfo.volType = DiskType::dt_muAF; break;
	case ID_MUPFS_DISK:volInfo.volType = DiskType::dt_muPFS; break;
	case ID_AFS_DISK:volInfo.volType = DiskType::dt_afs1; break;
	case ID_PFS2_DISK:volInfo.volType = DiskType::dt_pfs2; break;
	case 0x50465303L:volInfo.volType = DiskType::dt_pfs3; break;
	case ID_AFS_USER_TEST:volInfo.volType = DiskType::dt_AFSU; break;
	default: volInfo.volType = DiskType::dtUnknown; break;
	}

	memcpy_s(&volInfo.volumeLabel[0], volInfo.volumeLabel.length(), &g->rootblock->diskname[1], g->rootblock->diskname[0]);
	return true;
}

const uint8_t PFS3::MaxNameLength() { 
	return (uint8_t)FILENAMESIZE; 
};

// Get details about a file
PFS3::Error PFS3::GetFileInformation(const std::string& filename, FileInformation& fileDir) {
	objectinfo srcObj, srcFile;
	memset(&srcFile, 0, sizeof(srcFile));

	std::string leftOver;
	Error err = findObjectInfoForPath(filename, srcObj, leftOver);
	if (err != Error::eOK) return err;

	SIPTR error = 0;
	if (!FindObject(&srcObj, (char*)leftOver.c_str(), &srcFile, &error, g)) {
		if (error == PFS_ERROR_OBJECT_NOT_FOUND) return Error::eNotFound;
		return Error::eUnknown;
	}

	FileInfoBlock blk;
	FillFib(srcFile.file.direntry, &blk, g);

	fileDir.fileSize = blk.fib_Size;
	fileDir.isDirectory = blk.fib_DirEntryType > 0;
	PFS3_DateStamp2DateTime(&blk.fib_Date, &fileDir.modified);
	fileDir.protectBits = blk.fib_Protection;
	strcpy_s(fileDir.filename, &blk.fib_FileName[1]);

	return Error::eOK;
}

// Scan a directory
PFS3::Error PFS3::Dir(const std::string& path, std::function<void(const FileInformation& fileDir)> onFileDir) {
	objectinfo obj;
	std::string leftOver;
	Error perr;
	if (path.empty()) perr = findObjectInfoForPath(path, obj, leftOver); else {
		std::string tmp = path;
		if (tmp[tmp.length() - 1] != '/') tmp += "/";
		perr = findObjectInfoForPath(tmp, obj, leftOver);
	}
	if (perr != Error::eOK) return perr;

	listtype t;
	memset(&t, 0, sizeof(t));
	t.flags.type = IsVolume(obj) ? (ETF_VOLUME) : (ETF_LOCK);
	t.flags.access = ET_SHAREDREAD;
	t.flags.dir = 1;
	SIPTR err = 0;

	struct listentry* ent = MakeListEntry(&obj, t, &err, g);
	if (!ent) {
		if (err == PFS_ERROR_OBJECT_NOT_FOUND) return Error::eNotFound;
		if (err == PFS_ERROR_IS_SOFT_LINK) return Error::eNotDirectory;
		return Error::eUnknown;
	}

	FileInfoBlock blk;

	// This always returns true
	if (!ExamineFile(ent, &blk, &err, g)) {
		FreeListEntry(ent, g);
		return Error::eUnknown;
	}
	//onFileDir(blk); - this is the directory or volume name

	while (ExamineNextFile((lockentry_t*)ent, &blk, &err, g)) {
		FileInformation f;
		f.fileSize = blk.fib_Size;
		f.isDirectory = blk.fib_DirEntryType > 0;
		PFS3_DateStamp2DateTime(&blk.fib_Date, &f.modified);
		f.protectBits = blk.fib_Protection;
		strcpy_s(f.filename, &blk.fib_FileName[1]);
		onFileDir(f);
	}
	FreeListEntry(ent, g);

	if (err == PFS_ERROR_NO_MORE_ENTRIES) return Error::eOK;
	return Error::eUnknown;
}

// Close the file
void PFS3::Closefile(PFS3File& file) {
	if (!file) return;
	fileentry_t* fe = (fileentry_t*)file;

	if (fe->checknotify) {
		UpdateLE((listentry_t*)fe, g);
		Touch(&fe->le.info.file, g);
		FSIZE size = GetDEFileSize(fe->le.info.file.direntry, g);
		if (fe->originalsize != size)
			UpdateLinks(fe->le.info.file.direntry, g);
		fe->checknotify = 0;
	}
	RemoveListEntry((listentry_t*)fe, g);
	// FreeListEntry((listentry_t*)fe, g); RemoveListEntry does this
	file = nullptr;
	flushChanges(true);
}

// Read from a file
PFS3::Error PFS3::Readfile(PFS3File& file, void* data, uint32_t dataSize, uint32_t& dataRead) {
	listentry_t* listentry = (listentry_t*)file;
	if (!listentry) return Error::eUnknown;

	UpdateLE(listentry, g);

	SIPTR error = 0;
	LONG ret = ReadFromObject((fileentry_t*)listentry, (UBYTE *)data, (ULONG)dataSize, &error, g);
	if (ret < 0) {
		dataRead = 0;
		switch (error) {
		case PFS_ERROR_OBJECT_WRONG_TYPE: return Error::eNotFile;
		case PFS_ERROR_NO_FREE_STORE: return Error::eOutOfMem;
		default: return Error::eUnknown;
		}
	}
	else {
		dataRead = ret;
		return Error::eOK;
	}
}

// Write to a file
PFS3::Error PFS3::Writefile(PFS3File& file, const void* data, uint32_t dataSize, uint32_t& dataWritten) {
	listentry_t* listentry = (listentry_t*)file;
	if (!listentry) return Error::eUnknown;

	UpdateLE(listentry, g);

	SIPTR error = 0;
	LONG ret = WriteToObject((fileentry_t*)listentry, (UBYTE*)data, (ULONG)dataSize, &error, g);
	if (ret < 0) {
		dataWritten = 0;
		switch (error) {
		case PFS_ERROR_OBJECT_WRONG_TYPE: return Error::eNotFile;
		case PFS_ERROR_NO_FREE_STORE: return Error::eOutOfMem;
		case PFS_ERROR_WRITE_PROTECTED: return Error::eAccessDenied;
		case PFS_ERROR_DISK_FULL: return Error::eDiskFull;

		default: return Error::eUnknown;
		}		
	}
	else {
		dataWritten = ret;
		return Error::eOK;
	}
}

// Get the current file cursor position
PFS3::Error PFS3::GetfilePos(PFS3File& file, uint64_t& position) {
	listentry_t* listentry = (listentry_t*)file;
	if (!listentry) return Error::eUnknown;

	UpdateLE(listentry, g);

	position = (FSIZE)((fileentry_t*)listentry)->offset;
	return Error::eOK;
}

// Seek to a position into the file
PFS3::Error PFS3::Seekfile(PFS3File& file, uint64_t offset, SeekMode mode) {
	listentry_t* listentry = (listentry_t*)file;
	if (!listentry) return Error::eUnknown;

	UpdateLE(listentry, g);

	SIPTR error = 0;
	if (SeekInObject((fileentry_t*)listentry, (LONG)offset, (LONG)mode, &error, g)>=0)
		return PFS3::Error::eOK;

	switch (error) {
	case PFS_ERROR_OBJECT_WRONG_TYPE: return Error::eNotFile;
	case PFS_ERROR_DISK_WRITE_PROTECTED: return Error::eWriteProtected;
	case PFS_ERROR_NO_FREE_STORE: return Error::eOutOfMem;
	case PFS_ERROR_SEEK_ERROR: return Error::eSeekError;
		default: return Error::eUnknown; break;
	}	
}

// Truncate file to current position
PFS3::Error PFS3::GetfileSize(PFS3File& file, uint64_t& size) {
	fileentry_t* fileentry = (fileentry_t*)file;
	if (!fileentry) return Error::eUnknown;

	UpdateLE((listentry_t*)file, g);

#if ROLLOVER
	if (IsRollover(fileentry->le.info)) {
		struct extrafields fields;
		GetExtraFields(fileentry->le.info.file.direntry, &fields);
		size = fields.virtualsize;
		return Error::eOK;


	}
	else {
#endif
#if DELDIR
		struct deldirentry* delfile = nullptr;
		if (IsDelFile(fileentry->le.info))
			if (!(delfile = GetDeldirEntryQuick(fileentry->le.info.delfile.slotnr, g)))
				return Error::eUnknown;
		if (delfile)
			size = GetDDFileSize(delfile, g);
		else
#endif
			size = GetDEFileSize(fileentry->le.info.file.direntry, g);
		return Error::eOK;
	}
}

// Change the file size
PFS3::Error PFS3::ChangefileSize(PFS3File& file, uint64_t newSize) {
	listentry_t* listentry = (listentry_t*)file;
	if (!listentry) return Error::eUnknown;

	UpdateLE(listentry, g);
	SIPTR error = 0;
	if (ChangeObjectSize((fileentry_t*)listentry, newSize, OFFSET_BEGINNING, &error, g) >=0) {
		return PFS3::Error::eOK;
	}

	switch (error) {
	case PFS_ERROR_OBJECT_WRONG_TYPE: return Error::eNotFile;
	case PFS_ERROR_DISK_WRITE_PROTECTED: return Error::eWriteProtected;
	case PFS_ERROR_NO_FREE_STORE: return Error::eOutOfMem;
	case PFS_ERROR_SEEK_ERROR: return Error::eSeekError;
	default: return Error::eUnknown; break;
	}
}

// Truncate to the current file position
PFS3::Error PFS3::Truncatefile(PFS3File& file) {
	listentry_t* listentry = (listentry_t*)file;
	if (!listentry) return Error::eUnknown;

	UpdateLE(listentry, g);
	SIPTR error = 0;
	if (ChangeObjectSize((fileentry_t*)listentry, 0, OFFSET_CURRENT, &error, g) >=0) {
		return PFS3::Error::eOK;
	}

	switch (error) {
	case PFS_ERROR_OBJECT_WRONG_TYPE: return Error::eNotFile;
	case PFS_ERROR_DISK_WRITE_PROTECTED: return Error::eWriteProtected;
	case PFS_ERROR_NO_FREE_STORE: return Error::eOutOfMem;
	case PFS_ERROR_SEEK_ERROR: return Error::eSeekError;
	default: return Error::eUnknown; break;
	}
}


// Change file attributes
PFS3::Error PFS3::SetfileAttributes(const std::string& filename, uint32_t newAttributes) {
	objectinfo srcObj, srcFile;
	std::string leftOver;
	Error err = findObjectInfoForPath(filename, srcObj, leftOver);
	if (err != Error::eOK) return err;

	memset(&srcFile, 0, sizeof(srcFile));

	SIPTR error = 0;
	if (!FindObject(&srcObj, (char*)leftOver.c_str(), &srcFile, &error, g)) {
		if (error == PFS_ERROR_OBJECT_NOT_FOUND) return Error::eNotFound;
		if (error == PFS_ERROR_OBJECT_WRONG_TYPE) return Error::eNotFile;
		return Error::eUnknown;
	}

	if (ProtectFile(&srcFile.file, (ULONG)newAttributes, &error, g)) {
		flushChanges(true);
		return Error::eOK;
	}

	switch (error) {
	case PFS_ERROR_WRITE_PROTECTED: return Error::eAccessDenied;
	case PFS_ERROR_DISK_FULL: return Error::eDiskFull;
	default: return Error::eUnknown; break;
	}
}

// Change file date
PFS3::Error PFS3::SetfileDate(const std::string& filename, const struct tm& stamp) {
	objectinfo srcObj, srcFile;
	
	std::string leftOver;
	Error err = findObjectInfoForPath(filename, srcObj, leftOver);
	if (err != Error::eOK) return err;

	memset(&srcFile, 0, sizeof(srcFile));

	SIPTR error = 0;
	if (!FindObject(&srcObj, (char*)leftOver.c_str(), &srcFile, &error, g)) {
		if (error == PFS_ERROR_OBJECT_NOT_FOUND) return Error::eNotFound;
		if (error == PFS_ERROR_OBJECT_WRONG_TYPE) return Error::eNotFile;
		return Error::eUnknown;
	}

	DateStamp ds;
	PFS3_DateTime2DateStamp(&stamp, &ds);

	if (SetDate(&srcFile, &ds, &error, g)) {
		flushChanges(true);
		return Error::eOK;
	}

	switch (error) {
	case PFS_ERROR_WRITE_PROTECTED: return Error::eAccessDenied;
	case PFS_ERROR_DISK_FULL: return Error::eDiskFull;
	default: return Error::eUnknown; break;
	}
}

// Flush changes
PFS3::Error PFS3::Flushfile(PFS3File& file) {
	// Doesnt support flush!
	//flushChanges(false);
	return Error::eOK;
}

// Create/open file
PFS3::Error PFS3::Createfile(const std::string& filename, OpenMode openMode, bool readOnly, PFS3File& file) {
	objectinfo srcObj, srcFile;
	std::string leftOver;
	Error err = findObjectInfoForPath(filename, srcObj, leftOver);
	if (err != Error::eOK) return err;
	file = nullptr;

	listtype type;
	memset(&srcFile, 0, sizeof(srcFile));
	memset(&type, 0, sizeof(type));
	
	SIPTR error = 0;
	bool found = false;
	bool callNewFile = false;
	if (FindObject(&srcObj, (char*)leftOver.c_str(), &srcFile, &error, g)) {
		if (!IsFile(srcFile))
			return Error::eNotFile;
		found = true;
		if (openMode == OpenMode::omCreate) return Error::eAlreadyExists;

		callNewFile = (openMode == OpenMode::omCreateAlways) || (openMode == OpenMode::omTruncateExisting);
	}
	else {
		callNewFile = true;
		if (openMode == OpenMode::omOpenExisting) return Error::eNotFound;
		if (openMode == OpenMode::omTruncateExisting) return Error::eNotFound;
	}

	if (readOnly) {
		if (openMode == OpenMode::omCreate) return Error::eAccessDenied;		
		if (openMode == OpenMode::omTruncateExisting) return Error::eAccessDenied;
	}
	else
		if (g->softprotect) return Error::eWriteProtected;

	if (found) {
		/* softlinks cannot directly be opened */
		if (IsSoftLink(srcFile)) return Error::eCantOpenLink;
#if DELDIR
		if ((IsVolume(srcFile) || IsDelDir(srcFile) || IsDir(srcFile)))
#else
		if ((IsVolume(srcFile) || IsDir(srcFile)))
#endif
			return  Error::eNotFile;
	}

	type.value = ET_FILEENTRY;
	type.flags.access = readOnly ? ET_SHAREDREAD : ET_EXCLWRITE;

	if (callNewFile) {
		if (!found) memset(&srcFile, 0, sizeof(srcFile));
		ULONG res = NewFile(found, &srcObj, (STRPTR)leftOver.c_str(), &srcFile, g);
		if (res != 0) {
			switch (res) {
			case PFS_ERROR_WRITE_PROTECTED: return Error::eAccessDenied;
			case PFS_ERROR_DISK_FULL: return Error::eDiskFull;
			case PFS_ERROR_NO_FREE_STORE: return Error::eOutOfMem;
			case PFS_ERROR_OBJECT_NOT_FOUND: return Error::eNotFound;
			case PFS_ERROR_INVALID_COMPONENT_NAME: return Error::eBadName;
			default: return Error::eUnknown; break;
			}
		}	
	}

	listentry_t* filefe;

	/* Add file to list  */
	if (!(filefe = MakeListEntry(&srcFile, type, &error, g))) return Error::eUnknown;

	if (!AddListEntry(filefe)) {
		FreeListEntry(filefe, g);
		return Error::eInUse;
	}

	((fileentry_t*)filefe)->checknotify = callNewFile;
	
	file = filefe;
	//flushChanges(false);
	return Error::eOK;
}


PFS3::~PFS3() {
	if (g) {
		flushChanges(true);
		free(g);
	}
}

