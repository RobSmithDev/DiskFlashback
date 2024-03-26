/*
 * dir_test.c
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include"adflib.h"


void MyVer(char *msg)
{
    fprintf(stderr,"Verbose [%s]\n",msg);
}


/*
 *
 *
 */
int main(int argc, char *argv[])
{
    if ( argc < 2 ) {
        fprintf ( stderr,
                  "required parameter (image/device) absent - aborting...\n");
        return 1;
    }

    struct AdfDevice *hd;
    struct AdfVolume *vol;
    struct AdfFile *file;
    unsigned char buf[600];
    FILE *out;
 
    adfEnvInitDefault();

//	adfSetEnvFct(0,0,MyVer,0);

    /* mount existing device : FFS */
    hd = adfMountDev( argv[1],FALSE );
    if (!hd) {
        fprintf(stderr, "can't mount device\n");
        adfEnvCleanUp(); exit(1);
    }

    vol = adfMount(hd, 0, FALSE);
    if (!vol) {
        adfUnMountDev(hd);
        fprintf(stderr, "can't mount volume\n");
        adfEnvCleanUp(); exit(1);
    }

    adfVolumeInfo(vol);

    file = adfFileOpen ( vol, "mod.and.distantcall", ADF_FILE_MODE_READ );
    if (!file) return 1;
    out = fopen("mod.distant","wb");
    if (!out) return 1;
    
    unsigned len = 600;
    unsigned n = adfFileRead ( file, len, buf );
    while(!adfEndOfFile(file)) {
        fwrite(buf,sizeof(unsigned char),n,out);
        n = adfFileRead ( file, len, buf );
    }
    if (n>0)
        fwrite(buf,sizeof(unsigned char),n,out);

    fclose(out);

    adfFileClose ( file );

    file = adfFileOpen ( vol, "emptyfile", ADF_FILE_MODE_READ );
    if (!file) { 
		adfUnMount(vol); adfUnMountDev(hd); 
        fprintf(stderr, "can't open file\n");
		exit(1); 
	}
 
    n = adfFileRead ( file, 2, buf );

    adfFileClose ( file );

    adfUnMount(vol);
    adfUnMountDev(hd);


    /* ofs */

    hd = adfMountDev( argv[2],FALSE );
    if (!hd) {
        fprintf(stderr, "can't mount device\n");
        adfEnvCleanUp(); exit(1);
    }

    vol = adfMount(hd, 0, FALSE);
    if (!vol) {
        adfUnMountDev(hd);
        fprintf(stderr, "can't mount volume\n");
        adfEnvCleanUp(); exit(1);
    }

    adfVolumeInfo(vol);

    file = adfFileOpen ( vol, "moon.gif", ADF_FILE_MODE_READ );
    if (!file) return 1;
    out = fopen("moon_gif","wb");
    if (!out) return 1;

    len = 300;
    n = adfFileRead ( file, len, buf );
    while(!adfEndOfFile(file)) {
        fwrite(buf,sizeof(unsigned char),n,out);
        n = adfFileRead ( file, len, buf );
    }
    if (n>0)
        fwrite(buf,sizeof(unsigned char),n,out);

    fclose(out);

    adfFileClose ( file );

    adfUnMount(vol);
    adfUnMountDev(hd);


    adfEnvCleanUp();

    return 0;
}
