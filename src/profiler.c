#include "common.h"
#include "timer.h"
#include "symbols.h"
#include "profiler.h"

#define CATCOMP_NUMBERS
#include "locale_generated.h"
#include "locale.h"

#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct StackFrame {
    struct StackFrame* backChain;
    uint32* linkRegister;
};

typedef struct StackFrame StackFrame;

static void GetStackTrace(struct Task* task)
{
    static size_t stackTraceCounter = 0;

    const StackFrame* frame = task->tc_SPReg;
    const StackFrame* const lower = task->tc_SPLower;
    const StackFrame* const upper = task->tc_SPUpper;

    StackTraceSample* sample = &ctx.profiling.samples[stackTraceCounter];
    sample->task = task;

    for (size_t i = 0; i < MAX_STACK_DEPTH; i++) {
        if (frame && frame >= lower && frame < upper) {
            sample->addresses[i] = frame->linkRegister;
            if (frame == frame->backChain) {
                if (ctx.debugMode) {
                    IExec->DebugPrintF("Stack frame back chain loop %p\n", frame);
                }
                ctx.profiling.stackFrameLoopDetected++;
                frame = NULL;
                // TODO: how about identical/repeated IPs?
            } else {
                if (frame->backChain) {
                    const uint32 diff = (uint32)frame->backChain - (uint32)frame;
                    const uint32 modulo = diff % 16;
                    if (modulo) {
                        if (ctx.debugMode) {
                            IExec->DebugPrintF("Stack frames not aligned %p vs %p\n", frame, frame->backChain);
                        }
                        ctx.profiling.stackFrameNotAligned++;
                    }
                }
                frame = frame->backChain;
            }
        } else {
            if (frame) {
                if (ctx.debugMode) {
                    IExec->DebugPrintF("Stack frame pointer %p out of bounds\n", frame);
                }
                ctx.profiling.stackFrameOutOfBounds++;
            }
            sample->addresses[i] = NULL;
            break;
        }
    }

    if (++stackTraceCounter >= ctx.profiling.maxStackTraces) {
        stackTraceCounter = 0;
    }

    if (ctx.profiling.stackTraces < ctx.profiling.maxStackTraces) {
        ++ctx.profiling.stackTraces;
    }
}

void InterruptCode(void)
{
    BOOL quit = FALSE;
    struct MyClock start, finish;

    if (ctx.debugMode) {
        ITimer->ReadEClock(&start.un.clockVal);
    }

    struct ExecBase* sysbase = (struct ExecBase *)SysBase;
    struct Task* task = sysbase->ThisTask;
    static size_t counter = 0;

    ctx.back->sampleBuffer[counter].task = task;
    if (sysbase->TDNestCnt > 0) {
        ctx.back->forbidCount++;
    }

    if (ctx.profiling.enabled /*&& task == ctx.profiling.profiledTask*/) {
        GetStackTrace(task);
    }

    if (++counter >= ctx.totalSamples) {
        static int flip = 0;

        counter = 0;
        ctx.front = &ctx.sampleData[flip];
        flip ^= 1; // TODO: if main process doesn't get CPU, there might be glitches
        ctx.back = &ctx.sampleData[flip];
        ctx.back->uniqueTasks = 0;
        ctx.back->forbidCount = 0;

        //IExec->DebugPrintF("Signal %d -> main\n", mainSig);
        IExec->Signal(ctx.mainTask, 1L << ctx.timerSignal);
    }

    struct TimeRequest *request = (struct TimeRequest *)IExec->GetMsg(ctx.sampler.port);

    if (request && ctx.running) {
        TimerStart(request, ctx.period);
    } else {
        quit = TRUE;
    }

    if (ctx.debugMode) {
        ITimer->ReadEClock(&finish.un.clockVal);

        const uint64 duration = finish.un.ticks - start.un.ticks;
        if (duration > ctx.longestInterrupt) {
            ctx.longestInterrupt = duration;
        }
    }

    if (quit) {
        IExec->Signal(ctx.mainTask, 1L << ctx.lastSignal);
    }
}

static size_t CopyProcessData(struct Task* task, SampleInfo* info)
{
    ctx.cliNameBuffer[0] = '\0';

    if (IS_PROCESS(task)) {
        struct Process* process = (struct Process *)task;
        info->pid = process->pr_ProcessID;
        struct CommandLineInterface* cli = (struct CommandLineInterface *)BADDR(process->pr_CLI);
        if (cli) {
            const char* commandName = (const char *)BADDR(cli->cli_CommandName);
            if (commandName) {
                // This should create a string like " [command name]"
                ctx.cliNameBuffer[0] = ' ';
                ctx.cliNameBuffer[1] = '[';
                ctx.cliNameBuffer[2] = '\0';

                // cli_CommandName is a BSTR (should be NUL-terminated).
                // Remove path part
                const char* src = IDOS->FilePart(commandName + 1);
                const size_t len = strlcpy(ctx.cliNameBuffer + 2, src, NAME_LEN - 2);

                if (len < (NAME_LEN - 3)) {
                    ctx.cliNameBuffer[2 + len] = ']';
                    ctx.cliNameBuffer[3 + len] = '\0';
                    return len + 3;
                }

                return len + 2;
            }
        }
    }

    return 0;
}

static float GetStackUsage(struct Task* task)
{
    const ULONG totalStack = (ULONG)task->tc_SPUpper - (ULONG)task->tc_SPLower;
    const ULONG usedStack = (ULONG)task->tc_SPUpper - (ULONG)task->tc_SPReg;

    return 100.0f * (float)usedStack / (float)totalStack;
}

static BOOL Traverse(struct List* list, struct Task* target, SampleInfo* info)
{
    for (struct Node* node = IExec->GetHead(list); node; node = IExec->GetSucc(node)) {
        struct Task* task = (struct Task *)node;

        if (task == target) {
            const size_t cliNameLen = CopyProcessData(task, info);
            const size_t nameLen = strlen(node->ln_Name);

            strlcpy(info->nameBuffer, node->ln_Name, NAME_LEN);

            if (cliNameLen > 0) {
                strlcpy(info->nameBuffer + nameLen, ctx.cliNameBuffer, NAME_LEN - nameLen);
            }

            info->priority = node->ln_Pri;
            info->stackUsage = GetStackUsage(task);
            return TRUE;
        }
    }

    return FALSE;
}

static size_t GetTaskCount(struct List* list)
{
    size_t tasks = 0;
    for (struct Node* node = IExec->GetHead(list); node; node = IExec->GetSucc(node)) {
        tasks++;
    }

    return tasks;
}

size_t GetTotalTaskCount(void)
{
    struct ExecBase* eb = (struct ExecBase *)SysBase;
    size_t tasks = 0;

    IExec->Disable();

    tasks += GetTaskCount(&eb->TaskReady);
    tasks += GetTaskCount(&eb->TaskWait);

    IExec->Enable();

    return tasks;
}

static BOOL TraverseLists(struct Task* task, SampleInfo* info)
{
    struct ExecBase* eb = (struct ExecBase *)SysBase;

    IExec->Disable();

    BOOL found = Traverse(&eb->TaskReady, task, info);

    if (!found) {
        found = Traverse(&eb->TaskWait, task, info);
    }

    IExec->Enable();

    return found;
}

SampleInfo InitializeTaskData(struct Task* task)
{
    SampleInfo info;

    info.nameBuffer[0] = '\0';
    info.stackUsage = 0.0f;
    info.pid = 0;
    info.priority = 0;
    info.task = task;
    info.count = 1;

    const BOOL found = TraverseLists(task, &info);

    if (!found) {
        if (task == ctx.mainTask) {
            snprintf(info.nameBuffer, NAME_LEN, "* Tequila (%s)", GetString(MSG_THIS_TASK));
            info.stackUsage = GetStackUsage(task);
            info.pid = ((struct Process *)task)->pr_ProcessID;
            info.priority = ((struct Node *)task)->ln_Pri;
        } else {
            /* Could be some removed task */
            snprintf(info.nameBuffer, NAME_LEN, "%s %p", GetString(MSG_UNKNOWN_TASK), (void*)task);
        }
    }

    return info;
}

static int Comparison(const void* first, const void* second)
{
    const SampleInfo* a = first;
    const SampleInfo* b = second;

    if (a->count > b->count) return -1;
    if (a->count < b->count) return 1;

    return 0;
}

static void CalculateLoadAverages()
{
    static uint32 loadAverageCounter;
    uint32 i = 0;

    ctx.idleCpu = GetIdleCpu();
    if (ctx.idleCpu > 100.0f) {
        // glitch: idle CPU can exceed 100% when GUI is created / adjusted
        ctx.idleCpu = 100.0f;
    }

    const float cpu = 100.0f - ctx.idleCpu;

    const uint32 max1 = 60 / ctx.interval;
    const uint32 max5 = 5 * 60 / ctx.interval;
    const uint32 max15 = MAX_LOAD_AVERAGES / ctx.interval;

    const uint32 offset1 = 14 * 60 / ctx.interval;
    const uint32 offset5 = 10 * 60 / ctx.interval;

    ctx.loadAverage[loadAverageCounter++ % max15] = cpu;
    ctx.loadAverage1 = 0.0f;

    while (i < max1) {
        ctx.loadAverage1 += ctx.loadAverage[(loadAverageCounter + i + offset1) % max15];
        i++;
    }

    ctx.loadAverage5 = ctx.loadAverage1;

    while (i < max5) {
        ctx.loadAverage5 += ctx.loadAverage[(loadAverageCounter + i + offset5) % max15];
        i++;
    }

    ctx.loadAverage15 = ctx.loadAverage5;

    while (i < max15) {
        ctx.loadAverage15 += ctx.loadAverage[i];
        i++;
    }

    ctx.loadAverage1 /= (float)max1;
    ctx.loadAverage5 /= (float)max5;
    ctx.loadAverage15 /= (float)max15;
}

void PrepareResults(void)
{
    for (size_t sample = 0; sample < ctx.totalSamples; sample++) {
        struct Task* task = ctx.front->sampleBuffer[sample].task;

        BOOL found = FALSE;

        for (size_t i = 0; i < ctx.front->uniqueTasks; i++) {
            if (ctx.sampleInfo[i].task == task) {
                ctx.sampleInfo[i].count++;
                //IExec->DebugPrintF("count %u for task %p\n", results[i].count, task);
                found = TRUE;
                break;
            }
        }

        if (!found && ctx.front->uniqueTasks < MAX_TASKS) {
            ctx.sampleInfo[ctx.front->uniqueTasks++] = InitializeTaskData(task);
        }
    }

    qsort(ctx.sampleInfo, ctx.front->uniqueTasks, sizeof(SampleInfo), Comparison);

    const ULONG dispCount = ((struct ExecBase *)SysBase)->DispCount;

    ctx.taskSwitchesPerSecond = (dispCount - ctx.lastDispCount) / ctx.interval;
    ctx.lastDispCount = dispCount;

    CalculateLoadAverages();
}

float GetIdleCpu(void)
{
    float idleCpu = 0.0f;

    // Following tasks are considered idle.task which are running
    // when there is nothing else to schedule.
    const char* const knownIdleTaskNames[4] = {
        "idle.task", // AmigaOS 4 system idle.task
        "Uuno", // CPU Watcher
        "CPUClock.CPUTask", // CPUClock docky
        "CPUInfo.CPUTask", // CPUInfo docky
    };

    for (size_t i = 0; i < ctx.front->uniqueTasks; i++) {
        for (size_t n = 0; n < sizeof(knownIdleTaskNames) / sizeof(knownIdleTaskNames[0]); n++) {
            if (strcmp(ctx.sampleInfo[i].nameBuffer, knownIdleTaskNames[n]) == 0) {
                idleCpu += 100.0f * (float)ctx.sampleInfo[i].count / (float)ctx.totalSamples;
            }
        }
    }

    return idleCpu;
}

float GetForbidCpu(void)
{
    return 100.0f * (float)ctx.front->forbidCount / (float)ctx.totalSamples;
}

static void ShowResults(void)
{
    MyClock start, finish;

    if (ctx.debugMode) {
        ITimer->ReadEClock(&start.un.clockVal);
    }

    PrepareResults();

    printf("%cc[[ Tequila ]] - %s %3.1f%%. %s %3.1f%%. %s %u. %s %lu. %s %s\n",
           0x1B,
           GetString(MSG_IDLE),
           ctx.idleCpu,
           GetString(MSG_FORBID),
           GetForbidCpu(),
           GetString(MSG_TASKS),
           GetTotalTaskCount(),
           GetString(MSG_TASK_SWITCHES),
           ctx.taskSwitchesPerSecond,
           GetString(MSG_UPTIME),
           GetUptimeString());

    printf("Load average %3.1f %3.1f %3.1f\n", ctx.loadAverage1, ctx.loadAverage5, ctx.loadAverage15);

    printf("%-40s %6s %10s %10s %6s\n",
           GetString(MSG_COLUMN_TASK),
           GetString(MSG_COLUMN_CPU),
           GetString(MSG_COLUMN_PRIORITY),
           GetString(MSG_COLUMN_STACK),
           GetString(MSG_COLUMN_PID));

    for (size_t i = 0; i < ctx.front->uniqueTasks; i++) {
        SampleInfo* si = &ctx.sampleInfo[i];
        const float cpu = 100.0f * (float)si->count / (float)ctx.totalSamples;

        static char pidBuffer[16];

        if (si->pid > 0) {
            snprintf(pidBuffer, sizeof(pidBuffer), "%lu", si->pid);
        } else {
            snprintf(pidBuffer, sizeof(pidBuffer), "(task)");
        }

        printf("%-40s %6.1f %10d %10.1f %6s\n",
               si->nameBuffer,
               cpu,
               si->priority,
               si->stackUsage,
               pidBuffer);
    }

    if (ctx.debugMode) {
        ITimer->ReadEClock(&finish.un.clockVal);

        const uint64 duration = finish.un.ticks - start.un.ticks;

        if (duration > ctx.longestDisplayUpdate) {
            ctx.longestDisplayUpdate = duration;
        }

        printf("\nDisplay update %g us (longest %g us), longest interrupt %g us\n",
               TicksToMicros(duration),
               TicksToMicros(ctx.longestDisplayUpdate),
               TicksToMicros(ctx.longestInterrupt));
    }
}

void ShellLoop(void)
{
    const uint32 signalMask = 1L << ctx.timerSignal;

    while (ctx.running) {
        const uint32 wait = IExec->Wait(signalMask | SIGBREAKF_CTRL_C);

        if ((wait & signalMask) && ctx.profiling.showTaskDisplay) {
            ShowResults();
        }

        if (wait & SIGBREAKF_CTRL_C) {
            ctx.running = FALSE;
        }
    }
}

