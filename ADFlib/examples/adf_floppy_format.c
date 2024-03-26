
#include <adflib.h>
#include "adf_dev.h"
#include "adf_dev_flop.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>


void usage ( void );


int main ( int     argc,
           char ** argv )
{
    if ( argc < 4 ) {
        usage();
        return 1;
    }

    char * adfname = argv[1];
    char * label   = argv[2];

    errno = 0;
    unsigned long type = strtoul ( argv[3], NULL, 10 );
    if ( errno != 0 ) {
        fprintf ( stderr, "Incorrect fstype %ld.\n", type );
        return 1;
    }

    adfEnvInitDefault();

    struct AdfDevice * device = adfMountDev ( adfname, FALSE );
    if ( device ) {
        fprintf ( stderr, "The floppy disk %s already contains a filesystem - aborting...\n",
                  adfname );
        adfUnMountDev ( device );
        return 1;
    }

    device = adfOpenDev ( adfname, FALSE );
    if ( ! device ) {
        fprintf ( stderr, "Cannot open floppy disk %s - aborting...\n", adfname );
        return 1;
    }

    //adfDeviceInfo ( device );

    char *fdtype;
    int devtype = adfDevType ( device );
    if ( devtype == DEVTYPE_FLOPDD ) {
        device->sectors = 11;
        device->heads   = 2;
        fdtype          = "DD";
    } else if ( devtype == DEVTYPE_FLOPHD ) {
        device->sectors = 22;
        device->heads   = 2;
        fdtype          = "HD";
    } else { //if ( devtype == DEVTYPE_HARDDISK ) {
        fprintf ( stderr, "The device is not a floppy - aborting...\n" );
        return 1;
    }
    device->cylinders = device->size / ( device->sectors * device->heads * 512 );

    adfDeviceInfo ( device );

    printf ( "Formatting floppy (%s) disk '%s'...\n", fdtype, adfname );
    if ( adfCreateFlop ( device, label, (unsigned char) type ) != RC_OK ) {
        fprintf ( stderr, "Error formatting the disk image '%s'!", adfname );
        adfCloseDev ( device );
        adfEnvCleanUp();
        return 1;
    }
    printf ( "Done!\n" );
 
    adfCloseDev ( device );
    adfEnvCleanUp();

    return 0;
}


void usage ( void )
{
    printf ( "Usage:  adf_floppy_format filename label fstype\n\n"
//             " where:\n"
             "   fstype can be 0-7: flags = 3 least significant bits\n"
             "         bit  set         clr\n"
             "         0    FFS         OFS\n"
             "         1    INTL ONLY   NO_INTL ONLY\n"
             "         2    DIRC&INTL   NO_DIRC&INTL\n\n" );
}
