#pragma once

// Simple PFS3 Wrapper with Little-Big Endian conversion, by Robert Smith (@RobSmithDev)
// This original PFS3 code was developed by Michiel Pelt.
// This *single file* is available under the The Unlicense licence.

#include <functional>
#include <stdint.h>
#include <string>

#ifdef MAKEDLL
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT __declspec(dllimport)
#endif



class EXPORT IPFS3 {
public:
	enum class Error : uint32_t { eOK = 0, eWriteProtected, eAlreadyExists, eNotFound, eNotDirectory, eNotFile, eSeekError, eCantOpenLink, eDiskFull, eBadName, eNotEmpty, eInUse, eAccessDenied, eOutOfMem, eUnknown };
	enum class OpenMode : uint32_t { omCreate = 0, omCreateAlways, omOpenAlways, omOpenExisting, omTruncateExisting };
	enum class SeekMode : int32_t { smFromBeginning = -1, smFromEnd = 1, smFromCurrentPosition = 0 };
	enum class DiskType : uint32_t { dt_pfs1, dt_beta, dt_busy, dt_muAF, dt_muPFS, dt_afs1, dt_pfs2, dt_pfs3, dt_AFSU, dtUnknown };

public:
	static const uint32_t Protect_Script = (1 << 6);
	static const uint32_t Protect_Pure = (1 << 5);
	static const uint32_t Protect_Archive = (1 << 4);
	static const uint32_t Protect_Read = (1 << 3);
	static const uint32_t Protect_Write = (1 << 2);
	static const uint32_t Protect_Execute = (1 << 1);
	static const uint32_t Protect_Delete = (1 << 0);

	struct PFSVolInfo {
		bool readOnly;
		uint32_t totalBlocks;
		uint32_t blocksUsed;
		uint32_t bytesPerBlock;
		std::string volumeLabel;
		DiskType volType;
	};

	struct DriveInfo {
		uint32_t sectorSize;
		uint32_t cylBlocks;
		uint32_t numCylinders;
		uint32_t numHeads;
	};

	struct PartitionInfo {
		uint32_t blockSize;
		uint32_t sectorsPerBlock;
		uint32_t blocksPerTrack;
		uint32_t lowCyl;
		uint32_t highCyl;
		uint32_t mask;
		uint32_t numBuffer;
	};

	struct FileInformation {
		bool		isDirectory;
		char		filename[110];
		uint32_t	protectBits;
		uint32_t	fileSize;
		struct tm   modified;
	};

	// Used for Read Only
	static bool IsFileWriteProtected(uint32_t protectBits) { return (protectBits & Protect_Write) != 0; };
	static bool IsFileArchve(uint32_t protectBits) { return (protectBits & Protect_Archive) != 0; };

	typedef void* PFS3File;

	virtual const uint8_t MaxNameLength() = 0;
	
	static IPFS3* createInstance(const struct DriveInfo& drive, const struct PartitionInfo& partition, 
		const std::function<bool(uint32_t physicalSector, uint32_t readSize, void* data)> readSector, 
		const std::function<bool(uint32_t physicalSector, uint32_t readSize, const void* data)> writeSector,
		const std::function<void(const std::string& message)> pfsError,
		const bool readOnly);

	virtual ~IPFS3() {};

	// Is it available?
	virtual bool available() = 0;

	// Can return: eNotFound, eNotDirectory, eUnknown, eOK, eWriteProtected, eAlreadyExists, eDiskFull, eBadName
	virtual Error MkDir(const std::string& path) = 0;
	// Can return: eNotFound, eNotDirectory, eUnknown, eOK, eWriteProtected, eNotFound, eNotEmpty
	virtual Error RmDir(const std::string& path) = 0;
	// Can return: eNotFound, eNotDirectory, eUnknown, eOK, eWriteProtected, eNotFound
	virtual Error Deletefile(const std::string& path) = 0;

	// Can return: eNotFound, eNotDirectory, eUnknown, eOK, eWriteProtected, eDiskFull, eAlreadyExists, eInUse
	virtual Error Movefile(const std::string& originalName, const std::string& newName, const bool replaceExisting) = 0;

	// Get details about a volume
	virtual bool GetVolInformation(struct PFSVolInfo& volInfo) = 0;

	// Get details about a file
	virtual Error GetFileInformation(const std::string& path, FileInformation& fileDir) = 0;

	// Create/open file:   eNotFile, eAccessDenied
	virtual Error Createfile(const std::string& filename, OpenMode openMode, bool readOnly, PFS3File& file) = 0;

	// Seek to a position into the file.  Errors: eUnknown, eNotFile, eWriteProtected, eDiskFull, eFailed
	virtual Error Seekfile(PFS3File& file, uint64_t offset, SeekMode mode) = 0;

	// Get the current file cursor position
	virtual Error GetfilePos(PFS3File& file, uint64_t& position) = 0;

	// Truncate file to current position
	virtual Error ChangefileSize(PFS3File& file, uint64_t newSize) = 0;

	// Truncate file to current position
	virtual Error GetfileSize(PFS3File& file, uint64_t& size) = 0;

	// Truncate to the current file position
	virtual Error Truncatefile(PFS3File& file) = 0;

	// Flush changes
	virtual Error Flushfile(PFS3File& file) = 0;

	// Change file attributes - eNotFound, eNotFile, eUnknown
	virtual Error SetfileAttributes(const std::string& filename, uint32_t newAttributes) = 0;

	// Change file date - eNotFound, eNotFile, eUnknown
	virtual Error SetfileDate(const std::string& filename, const struct tm& stamp) = 0;

	// Read from a file
	virtual Error Readfile(PFS3File& file, void* data, uint32_t dataSize, uint32_t& dataRead) = 0;

	// Write to a file
	virtual Error Writefile(PFS3File& file, const void* data, uint32_t dataSize, uint32_t& dataWritten) = 0;

	// Close the file
	virtual void Closefile(PFS3File& file) = 0;

	// Flush changes to disk
	virtual void flushChanges(bool force) = 0;

	// Scan a directory with callback for files/dirs. ** Can return: eNotFound, eNotDirectory, eUnknown, eOK
	virtual Error Dir(const std::string& path, std::function<void(const FileInformation& fileDir)> onFileDir) = 0;
};
