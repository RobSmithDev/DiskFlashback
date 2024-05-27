#pragma once

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

//void InitStack(void);
void AfsDie(void);
uint32_t divide(uint32_t d0, uint16_t d1);
void ctodstr(const unsigned char* a0, unsigned char* a1);
void intltoupper(unsigned char* a0);
int intlcmp(const unsigned char* a0, const unsigned char* a1);
int intlcdcmp(const unsigned char* a0, const unsigned char* a1);

#ifndef __SASC
int stcd_i(const char *in, int *ivalue);
int stcu_d(char *out, unsigned int val);
#endif


#ifdef __cplusplus
}
#endif /* __cplusplus */