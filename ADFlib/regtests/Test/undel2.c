/*
 *  undel2.c
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
    (void) argc, (void) argv;
    struct AdfDevice *hd;
    struct AdfVolume *vol;
    struct AdfList *list, *cell;
    struct GenBlock *block;
    BOOL true = TRUE;
    struct AdfFile *file;
    unsigned char buf[600];
    FILE *out;
  
    adfEnvInitDefault();

    adfChgEnvProp(PR_USEDIRC,&true);
 
    hd = adfMountDev(argv[1], FALSE);
    if (!hd) {
        fprintf(stderr, "can't mount device\n");
        adfEnvCleanUp(); exit(1);
    }

    adfDeviceInfo(hd);

    vol = adfMount(hd, 0, FALSE);
    if (!vol) {
        adfUnMountDev(hd);
        fprintf(stderr, "can't mount volume\n");
        adfEnvCleanUp(); exit(1);
    }

    cell = list = adfGetDirEnt(vol, vol->curDirPtr);
    while(cell) {
        adfEntryPrint ( cell->content );
        cell = cell->next;
    }
    adfFreeDirList(list);
    adfVolumeInfo(vol);

    puts("\nremove mod.and.distantcall");
    adfRemoveEntry(vol,vol->curDirPtr,"mod.and.distantcall");
    adfVolumeInfo(vol);

    cell = list = adfGetDelEnt(vol);
    while(cell) {
        block =(struct GenBlock*) cell->content;
        printf ( "%s %d %d %d\n",
                 block->name,
                 block->type,
                 block->secType,
                 block->sect );
        cell = cell->next;
    }
    adfFreeDelList(list);

    adfCheckEntry(vol,886,0);
    adfUndelEntry(vol,vol->curDirPtr,886);
    puts("\nundel mod.and.distantcall");
    adfVolumeInfo(vol);

    cell = list = adfGetDirEnt(vol, vol->curDirPtr);
    while(cell) {
        adfEntryPrint ( cell->content );
        cell = cell->next;
    }
    adfFreeDirList(list);

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

    adfUnMount(vol);
    adfUnMountDev(hd);

    adfEnvCleanUp();

    return 0;
}
