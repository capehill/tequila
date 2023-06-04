#ifndef PROFILER_H
#define PROFILER_H

#include <stddef.h>

void InterruptCode(void);
void ShellLoop(void);
void PrepareResults(void);
size_t GetTotalTaskCount(void);
float GetIdleCpu(void);
float GetForbidCpu(void);

#endif
