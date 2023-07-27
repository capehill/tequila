#ifndef COMMON_H
#define COMMON_H

#include "timer.h"

#include <exec/types.h>
#include <stddef.h>

#define MAX_STACK_DEPTH 30
#define MAX_TASKS 100
#define NAME_LEN 256

// TODO: add documentation

typedef struct Sample {
    struct Task* task;
    //uint32 forbidCount; // TODO: could collect for each task. But it needs zeroing during flip
} Sample;

typedef struct SampleInfo {
    char nameBuffer[NAME_LEN];
    struct Task* task;
    unsigned count;
    float stackUsage;
    uint32 pid;
    BYTE priority;
} SampleInfo;

typedef struct SampleData {
    Sample* sampleBuffer;
    uint32 uniqueTasks;
    uint32 forbidCount;
} SampleData;

typedef struct Profiling {
    BOOL enabled;
    //struct Task* task; // TODO: consider focusing on a specific task
    ULONG** addresses;
    ULONG stackTraces;
    size_t validSymbols;
    size_t uniqueSymbols;
    size_t uniqueStackTraces;
} Profiling;

typedef struct Context {
    uint64 longestInterrupt;
    uint64 longestDisplayUpdate;

    ULONG period;
    ULONG samples;
    ULONG interval;
    ULONG totalSamples; // interval * samples
    ULONG taskSwitchesPerSecond;
    ULONG lastDispCount;

    BOOL debugMode;
    BOOL gui;
    BOOL running;
    BOOL customRendering;
    BOOL symbolLookupWorkaroundNeeded;

    BYTE timerSignal;
    BYTE lastSignal;
    struct Task* mainTask;
    struct Interrupt* interrupt;

    SampleInfo sampleInfo[MAX_TASKS];

    SampleData sampleData[2];
    SampleData* front;
    SampleData* back;

    char* cliNameBuffer;

    TimerContext sampler;

    Profiling profiling;
} Context;

extern Context ctx;

APTR AllocateMemory(size_t size);
void FreeMemory(APTR address);

#endif

