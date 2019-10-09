#ifndef COMMON_H
#define COMMON_H

#include <exec/types.h>
#include <stddef.h>

#define NAME_LEN 256

APTR allocMem(size_t size);
void freeMem(APTR address);
size_t stringLen(const char* str);
void copyString(char* const to, const char* const from, size_t len);

#endif

