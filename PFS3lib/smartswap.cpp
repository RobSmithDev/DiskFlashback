
#include "smartswap.h"

#include "struct.h"
#include "blocks.h"
#include "disk.h"


void SmartSwap(struct bitmapblock* block, uint32_t totalSize) {
#ifdef LITT_ENDIAN
	if (!totalSize) return;
	SmartSwap(block->id);
	SmartSwap(block->not_used);
	SmartSwap(block->datestamp);
	SmartSwap(block->seqnr);
	const uint32_t offset = offsetof(bitmapblock, bitmap);
	totalSize -= offset;
	ULONG* tmp = (ULONG*)(((uint8_t*)block) + offset);
	while (totalSize >= 4) {
		SmartSwap(*tmp);
		totalSize -= 4;
		tmp++;
	}
#endif
}

void SmartSwap(struct indexblock* block, uint32_t totalSize) {
#ifdef LITT_ENDIAN
	if (!totalSize) return;
	SmartSwap((struct bitmapblock*)block, totalSize);
#endif
}


void SmartSwap(struct anodeblock* block, uint32_t totalSize) {
#ifdef LITT_ENDIAN
	if (!totalSize) return;
	SmartSwap((struct bitmapblock*)block, totalSize);
#endif
}

void SmartSwap(struct direntry* block) {
#ifdef LITT_ENDIAN
	SmartSwap(block->anode);   
	SmartSwap(block->fsize);
	SmartSwap(block->creationday);
	SmartSwap(block->creationminute);
	SmartSwap(block->creationtick);  
#endif
}

  
void SmartSwap(struct dirblock* block) {
#ifdef LITT_ENDIAN
	SmartSwap(block->id);
	SmartSwap(block->not_used);
	SmartSwap(block->datestamp);
	SmartSwap(block->not_used_2[0]);
	SmartSwap(block->not_used_2[1]);
	SmartSwap(block->anodenr); 
	SmartSwap(block->parent);

	struct direntry*  entry = (struct direntry*)(&block->entries);
	while (entry->next)
	{
		SmartSwap(entry);
		entry = NEXTENTRY(entry);
	}
#endif
}

void SmartSwap(struct deldirentry* block) {
#ifdef LITT_ENDIAN
	SmartSwap(block->anodenr);   
	SmartSwap(block->fsize);
	SmartSwap(block->creationday);
	SmartSwap(block->creationminute);
	SmartSwap(block->creationtick);
	SmartSwap(block->fsizex);
#endif
}

void SmartSwap(struct deldirblock* block, uint32_t totalSize) {
#ifdef LITT_ENDIAN
	if (!totalSize) return;
	SmartSwap(block->id);
	SmartSwap(block->not_used);
	SmartSwap(block->datestamp);
	SmartSwap(block->seqnr);
	SmartSwap(block->not_used_2[0]);
	SmartSwap(block->not_used_2[1]);
	SmartSwap(block->not_used_3);
	SmartSwap(block->uid);
	SmartSwap(block->gid);
	SmartSwap(block->protection);
	SmartSwap(block->creationday);
	SmartSwap(block->creationminute);
	SmartSwap(block->creationtick);

	const uint32_t offset = offsetof(deldirblock, entries);
	totalSize -= offset;
	deldirentry* tmp = (deldirentry*)(((uint8_t*)block) + offset);
	while (totalSize >= sizeof(deldirentry)) {
		SmartSwap(tmp);
		totalSize -= sizeof(deldirentry);
		tmp++;
	}
#endif
}


void SmartSwap(struct postponed_op* block) {
#ifdef LITT_ENDIAN
	SmartSwap(block->operation_id);
	SmartSwap(block->argument1);
	SmartSwap(block->argument2);
	SmartSwap(block->argument3);
#endif
}

void SmartSwap(struct rootblockextension* block) {
#ifdef LITT_ENDIAN
	SmartSwap(block->id);
	SmartSwap(block->not_used_1);
	SmartSwap(block->ext_options);
	SmartSwap(block->datestamp);
	SmartSwap(block->pfs2version);
	SmartSwap(block->root_date[0]);
	SmartSwap(block->root_date[1]);
	SmartSwap(block->root_date[2]);
	SmartSwap(block->volume_date[0]);
	SmartSwap(block->volume_date[1]);
	SmartSwap(block->volume_date[2]);
	SmartSwap(&block->tobedone);
	SmartSwap(block->reserved_roving);
	SmartSwap(block->rovingbit);
	SmartSwap(block->curranseqnr);
	SmartSwap(block->deldirroving);
	SmartSwap(block->deldirsize);
	SmartSwap(block->fnsize);
	SmartSwap(block->not_used_2[0]);
	SmartSwap(block->not_used_2[1]);
	SmartSwap(block->not_used_2[2]);
	for (uint32_t i = 0; i <= MAXSUPER; i++) SmartSwap(block->superindex[i]);
	SmartSwap(block->dd_uid);
	SmartSwap(block->dd_gid);
	SmartSwap(block->dd_protection);
	SmartSwap(block->dd_creationday);
	SmartSwap(block->dd_creationminute);
	SmartSwap(block->dd_creationtick);
	SmartSwap(block->not_used_3);
	for (uint32_t i = 0; i < 32; i++) SmartSwap(block->deldir[i]);
	for (uint32_t i = 0; i < 17; i++) SmartSwap(block->dosenvec[i]);
#endif
}


void SmartSwap(struct rootblock* data) {
#ifdef LITT_ENDIAN
	SmartSwap(data->disktype);
	SmartSwap(data->options);
	SmartSwap(data->datestamp);
	SmartSwap(data->creationday);
	SmartSwap(data->creationminute);
	SmartSwap(data->creationtick);
	SmartSwap(data->protection);
	SmartSwap(data->lastreserved);
	SmartSwap(data->firstreserved);
	SmartSwap(data->reserved_free);
	SmartSwap(data->reserved_blksize);
	SmartSwap(data->rblkcluster);
	SmartSwap(data->blocksfree);
	SmartSwap(data->alwaysfree);
	SmartSwap(data->roving_ptr);
	SmartSwap(data->deldir);
	SmartSwap(data->disksize);
	SmartSwap(data->extension);
	SmartSwap(data->not_used);
	ULONG* m = (ULONG*)&data->idx;
	uint32_t count = sizeof(data->idx) / sizeof(ULONG);
	while (count) {
		count--;
		SmartSwap(*m);
		m++;
	}
#endif
}

// Hopefully thats all of them
uint32_t SmartRawWrite(struct cachedblock* blk, globaldata* g) {
#ifdef LITT_ENDIAN
	const UWORD blockID = ((UWORD*)(blk->data))[0];
	const ULONG blkNum = blk->blocknr;
	const uint32_t totalSize = RESCLUSTER * g->blocksize;
	ULONG err = 0;

	// Loop runs twice so the swap gets 'undone'
	for (uint32_t counter = 0; counter < 2; counter++) {
		switch (blockID) {
		case DBLKID: SmartSwap(&((struct cdirblock*)blk)->blk);
			break;
		case ABLKID: SmartSwap(&((struct canodeblock*)blk)->blk, totalSize);
			break;
		case IBLKID: SmartSwap(&((struct cindexblock*)blk)->blk, totalSize);
			break;
		case BMBLKID: SmartSwap(&((struct cbitmapblock*)blk)->blk, totalSize);
			break;
		case BMIBLKID: SmartSwap(&((struct cindexblock*)blk)->blk, totalSize);
			break;
		case DELDIRID: SmartSwap(&((struct cdeldirblock*)blk)->blk, totalSize);
			break;
		case SBLKID: SmartSwap(&((struct cindexblock*)blk)->blk, totalSize);
			break;
		case EXTENSIONID: SmartSwap(&((struct crootblockextension*)blk)->blk); // shouldnt get caled
			break;
		}
		if (counter == 0) {
			err = RawWrite((UBYTE*)&blk->data, RESCLUSTER, blkNum, g);
		}
	}
	return err;
#endif
	return 0;
}