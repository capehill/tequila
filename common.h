#ifndef COMMON_H
#define COMMON_H

#include "timer.h"

#include <exec/types.h>
#include <stddef.h>

#define NAME_LEN 256
#define MAX_TASKS 100

// TODO: documentation needed

typedef struct SampleInfo {
    char nameBuffer[NAME_LEN];
    struct Task* task;
    unsigned count;
    float stackUsage;
    uint32 pid;
    BYTE priority;
} SampleInfo;

typedef struct SampleData {
    SampleInfo sampleInfoBuffer[MAX_TASKS];
    uint32 forbidCount;
    uint32 uniqueTasks;
    uint32 overflowTasks;
} SampleData;

typedef struct Context {
    uint64 longestInterrupt;
    uint64 longestDisplayUpdate;

    ULONG period;
    ULONG samples;
    ULONG interval;
    ULONG taskSwitchesPerSecond;
    ULONG lastDispCount;

    BOOL debugMode;
    BOOL profile;
    BOOL gui;
    BOOL running;
    BOOL customRendering;

    BYTE timerSignal;
    BYTE lastSignal;
    struct Task* mainTask;
    struct Interrupt* interrupt;

    SampleData sampleData[2];
    SampleData* front;
    SampleData* back;

    char* cliNameBuffer;

    TimerContext sampler;

    ULONG** addresses;
    ULONG maxAddresses;
} Context;

extern Context ctx;

APTR AllocateMemory(size_t size);
void FreeMemory(APTR address);

#endif

