#ifndef COMMON_H
#define COMMON_H

#include "timer.h"

#include <exec/types.h>
#include <stddef.h>

#define NAME_LEN 256

// TODO: documentation needed

typedef struct TaskInfo {
    float stackUsage;
    uint32 pid;
    BYTE priority;
} TaskInfo;

typedef struct Sample {
    struct Task* task;
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
    uint32 forbidCount;
} SampleData;

typedef struct Context {
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
    TaskInfo taskInfo;

    SampleInfo* sampleInfo;

    SampleData sampleData[2];
    SampleData* front;
    SampleData* back;

    char* nameBuffer;
    char* cliNameBuffer;

    TimerContext sampler;

    ULONG** addresses;
    ULONG maxAddresses;
} Context;

extern Context ctx;

APTR AllocateMemory(size_t size);
void FreeMemory(APTR address);
size_t StringLen(const char* str);
void CopyString(char* const to, const char* const from, size_t len);

#endif

