#pragma once

#include <stdint.h>

#define LITT_ENDIAN
#define DISABLE_PROTECT

struct bitmapblock;
struct indexblock;
struct anodeblock;
struct dirblock;
struct deldirentry;
struct deldirblock;
struct postponed_op;
struct rootblockextension;
struct globaldata;
struct cachedblock;

void SmartSwap(struct bitmapblock* block, uint32_t totalSize);
void SmartSwap(struct indexblock* block, uint32_t totalSize);
void SmartSwap(struct anodeblock* block, uint32_t totalSize);
void SmartSwap(struct dirblock* block);
void SmartSwap(struct deldirentry* block);
void SmartSwap(struct deldirblock* block, uint32_t totalSize);
void SmartSwap(struct postponed_op* block);
void SmartSwap(struct rootblockextension* block);
void SmartSwap(struct rootblock* data);
uint32_t SmartRawWrite(struct cachedblock* blk, globaldata* g);