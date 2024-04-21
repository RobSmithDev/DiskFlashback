# DiskFlashback
A Windows package to use Floppy Disk Images Files & Real Floppy Disks in Windows, directly.

## Why?
Most of the USB floppy drives are terrible. And modifying a drive for DrawBridge removes the IBM/PC floppy disk support in Windows. Plus I was getting annoyed having to boot WinUAE just to copy a file to an ADF (or disk). So I built this - And all the silly playground arguments from childhood now behind us I gave a little love to the Atari ST too (untested, I don't have one!)...

## Features
- Mount ADF, DMS*, IMG, IMA, ST, MSA*, HDA, HDF and SCP* disk and hard drive files as virtual drives (* are read only)
- Supports AmigaDOS OFS/FFS DD & HD Disks
- Supports IBM/PC FAT12/16 DD 720k & HD 1.44Mb Disks
- Supports Atari ST FAT12/16 GemDOS Single and Double Sided normal & extended Disks
- Supports Dual Format Amiga/Atari floppy disks and mounts them as two drives! (read only)
- Use your DrawBridge, Greaseweazle or SupercardPRO board as a real floppy drive with direct access to files
- Can create new disk images of any of the above formats.
- Rip real floppy disks to the above formats
- Write disk images to real floppy disks
- Install boot blocks on Amiga floppy disks
- Optionally silently swap file extensions (eg: mod.thismusic to thismusic.mod)



## How does it work?
DiskFlashback is powered by:
- [Dokany](https://github.com/dokan-dev/dokany) for the Virtual Drives
- [ADFLib](https://github.com/lclevy/ADFlib) for the Amiga File System
- [FatFS](http://elm-chan.org/fsw/ff/) for the FAT12/16 File System
- [xDMS](https://zakalwe.fi/~shd/foss/xdms/) for DMS File Support
- [FloppyBridge](https://amiga.robsmithdev.co.uk/winuae) for Real Floppy Disk Support
DiskFlashback is OpenSource and available under the GPL2 Licence, and available from GitHub.

## Summary
DiskFlashback is OpenSource and available under the [GPL2 Licence](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html) and can be [downloaded from this site](https://robsmithdev.co.uk/diskflashback)

Join me on [Discord](https://discord.gg/HctVgSFEXu) for further discussions and also support me on [Patreon](https://www.patreon.com/RobSmithDev)
  
Enjoy...


      
