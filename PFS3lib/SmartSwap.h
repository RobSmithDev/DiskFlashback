#pragma once

#include <stdint.h>


#ifndef LITT_ENDIAN
#if defined(__hppa__) || \
     defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
     (defined(__MIPS__) && defined(__MISPEB__)) || \
     defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
     defined(__sparc__)
#else
#define LITT_ENDIAN 1
#endif
#endif
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