#ifndef COMMON_H
#define COMMON_H

#include "timer.h"

#include <exec/types.h>
#include <stddef.h>

#define MAX_STACK_DEPTH 30
#define MAX_TASKS 100
#define NAME_LEN 256

typedef struct Sample {
    struct Task* task; // Currently running task, collected by timer interrupt
    //uint32 forbidCount; // TODO: could collect for each task. But it needs zeroing during flip
} Sample;

typedef struct SampleInfo {
    char nameBuffer[NAME_LEN]; // Display name for task
    struct Task* task; // System task
    unsigned count; // Number of samples task was seen running
    float stackUsage; // % of stack used
    uint32 pid; // System process ID
    BYTE priority; // System task priority
} SampleInfo;

typedef struct SampleData {
    Sample* sampleBuffer; // Task data
    uint32 uniqueTasks; // Number of unique tasks identified
    uint32 forbidCount; // Number of samples collected with task switching disabled
} SampleData;

typedef struct StackTraceSample {
    struct Task* task; // Related task
    ULONG* addresses[MAX_STACK_DEPTH]; // Stores collected instruction pointers of collected stack traces
} StackTraceSample;

typedef struct Profiling {
    BOOL enabled; // TRUE when user enables profiling
    //struct Task* task; // TODO: consider focusing on a specific task
    StackTraceSample* samples;
    size_t stackTraces; // Number of stack traces collected
    size_t maxStackTraces; // 30 (seconds) * samples
    size_t validSymbols; // Number of valid symbols found. (For example, not NULL)
    size_t uniqueSymbols; // Number of unique symbols found
    size_t uniqueStackTraces; // Number of unique stack traces found
    size_t stackFrameLoopDetected; // When back chain pointer points to the current stack frame
    size_t stackFrameNotAligned; // When stack frame pointers don't have 16-byte relative alignment
    size_t stackFrameOutOfBounds; // When stack frame pointer exceeds lower or upper bound
} Profiling;

typedef struct Context {
    uint64 longestInterrupt; // Debug info about longest timer interrupt
    uint64 longestDisplayUpdate; // Debug info about longest display update

    ULONG period; // 1000000 microseconds / samples
    ULONG samples; // Samples collected per second
    ULONG interval; // Display update interval in seconds
    ULONG totalSamples; // interval * samples
    ULONG taskSwitchesPerSecond;
    ULONG lastDispCount;

    BOOL debugMode; // Display extra debug information when enabled
    BOOL gui; // Start in GUI mode
    BOOL running; // TRUE until program is quit
    BOOL customRendering; // Alternative (simpler and faster) GUI mode
    BOOL symbolLookupWorkaroundNeeded; // WA needed for kernel version <= 54.46

    BYTE timerSignal; // Signaled by timer interrupt when data enough data is collected for display
    BYTE lastSignal; // Signaled by timer interrupt when quitting. Main program waits for timer to "stop"
    struct Task* mainTask; // Tequila main program
    struct Interrupt* interrupt; // Tequila timer interrupt

    SampleInfo sampleInfo[MAX_TASKS]; // This data is refined for each unique task from SampleData

    SampleData sampleData[2]; // Double-buffered data for task monitoring, collected by timer interrupt
    SampleData* front; // Points to data being displayed
    SampleData* back; // Points to data being collected

    char* cliNameBuffer; // String buffer for shell process names

    TimerContext sampler; // Context for timer interrupt

    Profiling profiling; // Profiling-related data
} Context;

extern Context ctx;

APTR AllocateMemory(size_t size);
void FreeMemory(APTR address);

#endif

