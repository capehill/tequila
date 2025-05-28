#ifndef PROFILER_H
#define PROFILER_H

#include <stddef.h>

#include "common.h"

void InterruptCode(void);
void ShellLoop(void);
void PrepareResults(void);
size_t GetTotalTaskCount(void);
float GetIdleCpu(void);
float GetForbidCpu(void);
SampleInfo InitializeTaskData(struct Task* task);

#endif
