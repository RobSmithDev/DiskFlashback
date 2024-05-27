#include <stdlib.h>
#include <string.h>

#include "adf_nativedriver.h"
extern "C" {
#include "ADFlib/src/adflib.h"
#include "ADFlib/src/adf_blk.h"
#include "ADFlib/src/adf_env.h"
#include "ADFlib/src/adf_err.h"
#include "ADFlib/src/adf_dev.h"
}
#include "sectorCache.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void Warning(const char* const format, ...) {
#ifdef _DEBUG
#endif
}
void Error(const char* const format, ...) {
#ifdef _DEBUG
#endif
}
void Verbose(const char* const format, ...) {
#ifdef _DEBUG    
#endif
}

void adfPrepNativeDriver() {
    adfEnvInitDefault();
    adfEnvSetFct((AdfLogFct)Error, (AdfLogFct)Warning, (AdfLogFct)Verbose, NULL);
    adfAddDeviceDriver(&adfDeviceDriverDiskFlashback);
}

int adfDevType(struct AdfDevice* dev)
{
    if ((dev->size == 512 * 11 * 2 * 80) ||		/* BV */
        (dev->size == 512 * 11 * 2 * 81) ||		/* BV */
        (dev->size == 512 * 11 * 2 * 82) || 	/* BV */
        (dev->size == 512 * 11 * 2 * 83))		/* BV */
        return(ADF_DEVTYPE_FLOPDD);
    else if ((dev->size == 512 * 22 * 2 * 80) ||
        (dev->size == 512 * 22 * 2 * 81) ||
        (dev->size == 512 * 22 * 2 * 82) ||
        (dev->size == 512 * 22 * 2 * 83))
        return(ADF_DEVTYPE_FLOPHD);
    else if (dev->size > 512 * 22 * 2 * 83)
        return(ADF_DEVTYPE_HARDDISK);
    else {       
        return(-1);
    }
}

static struct AdfDevice* dfbCreate(const char* const name,
    const uint32_t     cylinders,
    const uint32_t     heads,
    const uint32_t     sectors)
{
    struct AdfDevice* dev = (struct AdfDevice*) malloc(sizeof(struct AdfDevice));
    if (dev == NULL) return NULL;

    dev->readOnly = false; // ( mode != ADF_ACCESS_MODE_READWRITE );
    dev->heads = heads;
    dev->sectors = sectors;
    dev->cylinders = cylinders;
    dev->size = cylinders * heads * sectors * 512;

    dev->drvData = (void*)name;
    dev->devType = (AdfDeviceType)adfDevType(dev);
    dev->nVol = 0;
    dev->volList = NULL;
    dev->mounted = false;
    dev->name = (char*)DISKFLASHBACK_AMIGA_DRIVER;
    dev->drv = &adfDeviceDriverDiskFlashback;

    return dev;
}

struct AdfDevice* (*openDev) (const char* const  name,
    const AdfAccessMode mode);

static struct AdfDevice* dfbOpen(const char* const name, const AdfAccessMode mode) {
    struct AdfDevice* dev = (struct AdfDevice*)malloc(sizeof(struct AdfDevice));
    if (dev == NULL) return NULL;
    SectorCacheEngine* cache = (SectorCacheEngine*)name;

    dev->readOnly = ( mode != ADF_ACCESS_MODE_READWRITE );
    if (cache->isPhysicalDisk()) {
        dev->heads = cache->getNumHeads();
        dev->sectors = cache->numSectorsPerTrack();
        dev->cylinders = 80;
        dev->size = cache->getNumHeads() * cache->numSectorsPerTrack() * dev->cylinders * 512;
    }
    else {
        dev->heads = 0;
        dev->sectors = 0;
        dev->cylinders = 0;
        dev->size = cache->getDiskDataSize();
    }

    dev->drvData = (void*)name;
    dev->devType = (AdfDeviceType)adfDevType(dev);
    dev->nVol = 0;
    dev->volList = NULL;
    dev->mounted = false;
    dev->name = (char*)DISKFLASHBACK_AMIGA_DRIVER;
    dev->drv = &adfDeviceDriverDiskFlashback;

    return dev;
}


static ADF_RETCODE dfbRelease(struct AdfDevice* const dev) {
    free(dev);
    return ADF_RC_OK;
}


static ADF_RETCODE dfbReadSector(struct AdfDevice* const dev, const uint32_t n, const unsigned size, uint8_t* const buf) {    
    SectorCacheEngine* d = (SectorCacheEngine*)dev->drvData;

    if (size != 512) {
        uint8_t buffer[512];
        if (!d->readData(n, 512, buffer)) return ADF_RC_ERROR;
        memcpy_s(buf, size, buffer, min(size, 512));
        return ADF_RC_OK;
    }

    return d->readData(n, size, buf) ? ADF_RC_OK : ADF_RC_ERROR;
}

static ADF_RETCODE dfbWriteSector(struct AdfDevice* const dev, const uint32_t n, const unsigned size, const uint8_t* const    buf) {
    SectorCacheEngine* d = (SectorCacheEngine*)dev->drvData;

    if (size != 512) {
        uint8_t buffer[512];
        if (!d->readData(n, 512, buffer)) return ADF_RC_ERROR;
        memcpy_s(buffer, size, buf, min(size, 512));
        return d->writeData(n, 512, buffer) ? ADF_RC_OK : ADF_RC_ERROR;
    }

    return d->writeData(n, size, buf) ? ADF_RC_OK : ADF_RC_ERROR;
}


static bool dfbIsDevNative(void) {
    return false;
}


const struct AdfDeviceDriver adfDeviceDriverDiskFlashback = {
    .name = DISKFLASHBACK_AMIGA_DRIVER,
    .data = NULL,
    .createDev = dfbCreate,
    .openDev = dfbOpen,
    .closeDev = dfbRelease,
    .readSector = dfbReadSector,
    .writeSector = dfbWriteSector,
    .isNative = dfbIsDevNative,
    .isDevice = NULL
};
