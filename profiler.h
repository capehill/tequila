#ifndef PROFILER_H
#define PROFILER_H

#include <stddef.h>

void InterruptCode(void);
void ShellLoop(void);
size_t PrepareResults(void);
size_t GetTotalTaskCount(void);
float GetIdleCpu(const size_t count);

#endif
