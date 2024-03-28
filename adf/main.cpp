

#include <string>
#include <vector>

#include "adf.h"
#include "adflib/src/adflib.h"
#include "adf_operations.h"
#include <errno.h>
#include "sectorCache.h"
#include "readwrite_file.h"
#include "readwrite_floppybridge.h"

std::vector<fs*> dokan_fs;
struct AdfDevice* adfFile = nullptr;


BOOL WINAPI ctrl_handler(DWORD dw_ctrl_type) {
    switch (dw_ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            SetConsoleCtrlHandler(ctrl_handler, FALSE);
            for (auto& fs : dokan_fs) fs->stop();
            return TRUE;
        default:
            return FALSE;
    }
}


void Warning(char* msg) {
#ifdef _DEBUG
    fprintf(stderr,"Warning <%s>\n",msg);
#endif
}

void Error(char* msg) {
#ifdef _DEBUG
    fprintf(stderr,"Error <%s>\n",msg);
#endif
   // exit(1);
}

void Verbose(char* msg) {
#ifdef _DEBUG
    fprintf(stderr,"Verbose <%s>\n",msg);
#endif
}



RETCODE adfInitDevice(struct AdfDevice* const dev, const char* const name, const BOOL ro) {    
    SectorRW_FloppyBridge* d = new SectorRW_FloppyBridge("");
    /*

    HANDLE fle = CreateFileA(name, GENERIC_READ | (ro ? 0 : GENERIC_WRITE), 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, 0);
    if (fle == INVALID_HANDLE_VALUE) {
        int err = GetLastError();
        return RC_ERROR;
    }

    // Default the cache to allow a entire HD floppy disk to get into the cache. 
    NativeDeviceCached* d = new NativeDeviceCached(512*84*2*2*11, fle);
    if (!d) {
        CloseHandle(fle);
        return RC_ERROR;
    }*/

    //dev->size = GetFileSize(fle, NULL);

    dev->size = d->getDiskDataSize();

    dev->nativeDev = (void*)d;

    return RC_OK;
}

RETCODE adfReleaseDevice(struct AdfDevice* const dev) {
    SectorCacheEngine* d = (SectorCacheEngine*)dev->nativeDev;
    if (d) {
        delete d;
        dev->nativeDev = nullptr;
    }
    return RC_OK;
}

RETCODE adfNativeReadSector(struct AdfDevice* const dev, const uint32_t n, const unsigned size, uint8_t* const buf) {    
    SectorCacheEngine* d = (SectorCacheEngine*)dev->nativeDev;
    return d->readData(n, size, buf) ? RC_OK : RC_ERROR;
}

RETCODE adfNativeWriteSector(struct AdfDevice* const dev, const uint32_t n, const unsigned size, const uint8_t* const buf) {
    SectorCacheEngine* d = (SectorCacheEngine*)dev->nativeDev;
    return d->writeData(n, size, buf) ? RC_OK : RC_ERROR;
}

BOOL adfIsDevNative(const char* const devName) {
    // If disk starts with DOS then its not a native device
    return true;
}


int __cdecl wmain(ULONG argc, PWCHAR argv[]) {
    adfEnvInitDefault();
    mainEXE = argv[0];

    adfSetEnvFct((AdfLogFct)Error, (AdfLogFct)Warning, (AdfLogFct)Verbose, NULL);

    struct AdfNativeFunctions native;
    native.adfInitDevice = adfInitDevice;
    native.adfReleaseDevice = adfReleaseDevice;
    native.adfNativeReadSector = adfNativeReadSector;
    native.adfNativeWriteSector = adfNativeWriteSector;
    native.adfIsDevNative = adfIsDevNative;
    adfSetNative(&native);
    
    try {
        if (!SetConsoleCtrlHandler(ctrl_handler, TRUE)) return 1;
        if (argc < 3) return 2;
        BOOL readOnly = FALSE; // TODO
            // Bad letter
        WCHAR letter = argv[2][0];
        if ((letter < 'A') || (letter > 'Z')) return 3;

        if (std::wstring(argv[1]) == L"FILE") {
            std::string ansiFilename;
            wideToAnsi(argv[3], ansiFilename);

            adfFile = adfMountDev(ansiFilename.c_str(), readOnly);
            
            if (!adfFile) return 4;
        }
        else 
            if (std::wstring(argv[1]) == L"DRIVE") {
                if (argc < 5) return 2;

                std::string ansiConfig;
                wideToAnsi(argv[3], ansiConfig);

              
                /* ARG 3 = Driver Type (DrawBridge, Greaseweazle, Supercard Pro)
                bridge = FloppyBridgeAPI::createDriver(_wtoi(argv[3]));
                if (!bridge) return 4;

                int comPort = _wtoi(argv[4]);

                bridge->setBridgeMode(FloppyBridge::BridgeMode::bmStalling);
                bridge->setComPortAutoDetect(comPort == 0);
                if (comPort) {
                    std::wstring port = L"\\\\.\\COM" + std::to_wstring(comPort);
                    bridge->setComPort((TCHAR*)port.c_str());
                }*/
            }

        // Attempt to mount all drives
        for (int volumeNumber = 0; volumeNumber < adfFile->nVol; volumeNumber++) {
            AdfVolume* vol = adfMount(adfFile, volumeNumber, readOnly);
            if (vol) {
                dokan_fs.push_back(new fs(adfFile, vol, letter, readOnly));
                letter++;
            }
        }

        DokanInit();

        // Start them up!
        for (auto& fs : dokan_fs) fs->start();

        // Start the memory filesystem
        bool running;
        do {
            running = false;
            for (auto& fs : dokan_fs) {
                fs->wait(250);
                running |= fs->isRunning();
            }
        } while (running);

        DokanShutdown();
    } catch (const std::exception& ex) {
        UNREFERENCED_PARAMETER(ex);
    }
    for (auto& fs : dokan_fs) fs->stop();

    if (adfFile) {
        adfUnMountDev(adfFile);
        adfFile = nullptr;
    }

    adfEnvCleanUp();

    return 0;
}
