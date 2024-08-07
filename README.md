# DiskFlashback
A Windows package to use Floppy Disk Images Files & Real Floppy Disks in Windows, directly.

## Why?
I wanted to be able to access my Amiga and other systems floppy disks without having to boot an emulator. I wanted to be able to open a disk image file (like ADF, IMG etc) in Windows in the easiest way possible, as well as be able to use a real floppy drive too and see whats on it.
So I decided to build this - which allows you to view and edit the contents of real disks (floppy and hard drive) and disk image files directly from Windows Explorer, and yes, you can even read non-PC floppy and hard disks in real time too (hardware required for floppy disks).
Checkout https://robsmithdev.co.uk/diskflashback for more details!

## Features
- Mount ADF, DMS*, IMG, IMA, ST, MSA*, HDA, HDF, DSK (MSX) and SCP* files as virtual drives (* read only)
- Supports AmigaDOS OFS/FFS/PFS File Systems for Floppy & Hard Disks
- Supports IBM/PC FAT12/16 DD 720k & HD 1.44Mb Disks
- Supports Atari ST FAT12/16 GemDOS Single and Double Sided normal & extended Disks
- Supports MSX FAT12 File system (some characters will be incorrect)
- Supports Dual/Tripple Format Amiga/Atari floppy disks and mounts them as two drives! (read only)
- Mount your DrawBridge, Greaseweazle or SupercardPro as a real floppy drive in Windows
- Mount your real Amiga Hard Disks/Memory Cards as drives
- Create blank disk images for the above formats.
- Rip real floppy disks to file (sector based files only)
- Write disk images to real floppy disks
- Install boot blocks on Amiga disks
- Format floppy disks in a range of formats
- Optionally silently swap Amiga file extensions (eg: mod.thismusic to thismusic.mod)
- Provides a simple floppy drive head cleaner function

## How does it work?
DiskFlashback is powered by:
- [Dokany](https://github.com/dokan-dev/dokany) for the Virtual Drives
- [ADFLib](https://github.com/lclevy/ADFlib) for the Amiga File System
- [FatFS](http://elm-chan.org/fsw/ff/) for the FAT12/16 File System
- [xDMS](https://zakalwe.fi/~shd/foss/xdms/) for DMS File Support
- [pfs3aio](https://github.com/tonioni/pfs3aio) for the Amiga PFS3 File System
- [FloppyBridge](https://amiga.robsmithdev.co.uk/winuae) for Real Floppy Disk Support
DiskFlashback is OpenSource and available under the GPL2 Licence, and available from GitHub.

## Notes for Developers
You can trigger DiskFlashback to release control of the physical floppy drive (Drawbridge, Greaseweazle and SupercardPRO), and have it restore access too using a simple Windows command.  DiskFlashback will also automatically restore access after the application making the request terminates.

```  
void releaseVirtualDrives(bool release, int controllerType) {
	HWND remoteWindow = FindWindow(L"VIRTUALDRIVE_CONTROLLER_CLASS", L"DiskFlashback Tray Control");
	if (remoteWindow) SendMessage(remoteWindow, WM_USER + 2, (controllerType & 0x7FFF) | (release ? 0 : 0x8000), (LPARAM)GetCurrentProcessId());
}
```
*controllerType* should be: 0 (DrawBridge), 1 (Greaseweazle) or 2 (SupercardPRO)



## Summary
DiskFlashback is OpenSource and available under the [GPL2 Licence](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html) and can be [downloaded from this site](https://robsmithdev.co.uk/diskflashback)


Join me on [Discord](https://discord.gg/HctVgSFEXu) for further discussions and also support me on [Patreon](https://www.patreon.com/RobSmithDev)
  
Enjoy...


      
