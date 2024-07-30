#pragma once

extern "C" {
#include "adflib/src/adf_dev_driver.h"
}

#define DISKFLASHBACK_AMIGA_DRIVER "DISKFLASHBACK"

extern const struct AdfDeviceDriver adfDeviceDriverDiskFlashback;

void adfPrepNativeDriver();

