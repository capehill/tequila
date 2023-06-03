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

static uint64 longestInterrupt;

void InterruptCode(void)
{
    BOOL quit = FALSE;
    struct MyClock start, finish;

    if (ctx.debugMode) {
        ITimer->ReadEClock(&start.un.clockVal);
    }

    struct ExecBase* sysbase = (struct ExecBase *)SysBase;
    struct Task* task = sysbase->ThisTask;
    static unsigned counter = 0;

    ctx.back->sampleBuffer[counter].task = task;
    if (sysbase->TDNestCnt > 0) {
        ctx.back->forbidCount++;
    }

    if (ctx.profile) {
        static unsigned addressCounter = 0;

        uint32 *sp = task->tc_SPReg;
        uint32 address = sp ? *(sp + 1) : 0;

        if (sp > (uint32*)task->tc_SPUpper || sp < (uint32*)task->tc_SPLower) {
            IExec->DebugPrintF("SP %p\n", sp);
        }

        ctx.addresses[addressCounter] = (ULONG *)address;

        if (++addressCounter >= ctx.maxAddresses) {
            addressCounter = 0;
        }
    }

    if (++counter >= ctx.totalSamples) {
        static int flip = 0;

        counter = 0;
        ctx.front = &ctx.sampleData[flip];
        flip ^= 1; // TODO: if main process doesn't get CPU, there might be glitches
        ctx.back = &ctx.sampleData[flip];
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
        if (duration > longestInterrupt) {
            longestInterrupt = duration;
        }
    }

    if (quit) {
        IExec->Signal(ctx.mainTask, 1L << ctx.lastSignal);
    }
}

static size_t GetCliName(struct Task* task)
{
    ctx.cliNameBuffer[0] = '\0';

    if (IS_PROCESS(task)) {
        struct Process* process = (struct Process *)task;
        ctx.taskInfo.pid = process->pr_ProcessID;
        struct CommandLineInterface* cli = (struct CommandLineInterface *)BADDR(((struct Process *)task)->pr_CLI);
        if (cli) {
            const char* commandName = (const char *)BADDR(cli->cli_CommandName);
            if (commandName) {
                // This should create a string like " [command name]"
                ctx.cliNameBuffer[0] = ' ';
                ctx.cliNameBuffer[1] = '[';
                ctx.cliNameBuffer[2] = '\0';

                // BSTR (should be NUL-terminated)
                size_t len = *(UBYTE *)commandName;
                strlcpy(ctx.cliNameBuffer + 2, commandName + 1, NAME_LEN - 2);

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
    const float totalStack = task->tc_SPUpper - task->tc_SPLower;
    const float usedStack = task->tc_SPUpper - task->tc_SPReg;

    return 100.0f * usedStack / totalStack;
}

static BOOL Traverse(struct List* list, struct Task* target)
{
    for (struct Node* node = IExec->GetHead(list); node; node = IExec->GetSucc(node)) {
        struct Task* task = (struct Task *)node;

        if (task == target) {
            const size_t cliNameLen = GetCliName(task);
            const size_t nameLen = strlen(node->ln_Name);

            strlcpy(ctx.nameBuffer, node->ln_Name, NAME_LEN);

            if (cliNameLen > 0) {
                strlcpy(ctx.nameBuffer + nameLen, ctx.cliNameBuffer, NAME_LEN - nameLen);
            }

            ctx.taskInfo.priority = node->ln_Pri;
            ctx.taskInfo.stackUsage = GetStackUsage(task);
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

    IExec->Forbid();

    tasks += GetTaskCount(&eb->TaskReady);
    tasks += GetTaskCount(&eb->TaskWait);

    IExec->Permit();

    return tasks;
}

static BOOL TraverseLists(struct Task* task)
{
    struct ExecBase* eb = (struct ExecBase *)SysBase;

    IExec->Forbid();

    BOOL found = Traverse(&eb->TaskReady, task);

    if (!found) {
        found = Traverse(&eb->TaskWait, task);
    }

    IExec->Permit();

    return found;
}

static SampleInfo InitializeTaskData(struct Task* task)
{
    SampleInfo info;

    ctx.nameBuffer[0] = '\0';
    ctx.taskInfo.stackUsage = 0.0f;
    ctx.taskInfo.pid = 0;
    ctx.taskInfo.priority = 0;

    const BOOL found = TraverseLists(task);

    if (found) {
        snprintf(info.nameBuffer, NAME_LEN, ctx.nameBuffer);
    } else {
        if (task == ctx.mainTask) {
            snprintf(info.nameBuffer, NAME_LEN, "* Tequila (%s)", GetString(MSG_THIS_TASK));
            ctx.taskInfo.stackUsage = GetStackUsage(task);
            ctx.taskInfo.pid = ((struct Process *)task)->pr_ProcessID;
            ctx.taskInfo.priority = ((struct Node *)task)->ln_Pri;
        } else {
            /* Could be some removed task */
            snprintf(info.nameBuffer, NAME_LEN, "%s %p", GetString(MSG_UNKNOWN_TASK), task);
        }
    }

    info.task = task;
    info.count = 1;
    info.stackUsage = ctx.taskInfo.stackUsage;
    info.pid = ctx.taskInfo.pid;
    info.priority = ctx.taskInfo.priority;

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

size_t PrepareResults(void)
{
    size_t unique = 0;

    for (size_t sample = 0; sample < ctx.totalSamples; sample++) {
        struct Task* task = ctx.front->sampleBuffer[sample].task;

        BOOL found = FALSE;

        for (size_t i = 0; i < unique; i++) {
            if (ctx.sampleInfo[i].task == task) {
                ctx.sampleInfo[i].count++;
                //IExec->DebugPrintF("count %u for task %p\n", results[i].count, task);
                found = TRUE;
                break;
            }
        }

        if (!found) {
            ctx.sampleInfo[unique] = InitializeTaskData(task);
            unique++;
        }
    }

    qsort(ctx.sampleInfo, unique, sizeof(SampleInfo), Comparison);

    const ULONG dispCount = ((struct ExecBase *)SysBase)->DispCount;

    ctx.taskSwitchesPerSecond = (dispCount - ctx.lastDispCount) / ctx.interval;
    ctx.lastDispCount = dispCount;

    return unique;
}

float GetIdleCpu(const size_t count)
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

    for (size_t i = 0; i < count; i++) {
        for (size_t n = 0; n < sizeof(knownIdleTaskNames) / sizeof(knownIdleTaskNames[0]); n++) {
            if (strcmp(ctx.sampleInfo[i].nameBuffer, knownIdleTaskNames[n]) == 0) {
                idleCpu += 100.0f * ctx.sampleInfo[i].count / ctx.totalSamples;
            }
        }
    }

    return idleCpu;
}

float GetForbidCpu(void)
{
    const float forbid = 100.0f * ctx.front->forbidCount / ctx.totalSamples;
    ctx.front->forbidCount = 0;
    return forbid;
}

static void ShowResults(void)
{
    MyClock start, finish;

    if (ctx.debugMode) {
        ITimer->ReadEClock(&start.un.clockVal);
    }

    const size_t unique = PrepareResults();

    printf("%cc[[ Tequila ]] - %s %3.1f%%. %s %3.1f%%. %s %u. %s %lu. %s %s\n",
           0x1B,
           GetString(MSG_IDLE),
           GetIdleCpu(unique),
           GetString(MSG_FORBID),
           GetForbidCpu(),
           GetString(MSG_TASKS),
           GetTotalTaskCount(),
           GetString(MSG_TASK_SWITCHES),
           ctx.taskSwitchesPerSecond,
           GetString(MSG_UPTIME),
           GetUptimeString());

    printf("%-40s %6s %10s %10s %6s\n",
           GetString(MSG_COLUMN_TASK),
           GetString(MSG_COLUMN_CPU),
           GetString(MSG_COLUMN_PRIORITY),
           GetString(MSG_COLUMN_STACK),
           GetString(MSG_COLUMN_PID));

    for (size_t i = 0; i < unique; i++) {
        const float cpu = 100.0f * ctx.sampleInfo[i].count / ctx.totalSamples;

        static char pidBuffer[16];

        if (ctx.sampleInfo[i].pid > 0) {
            snprintf(pidBuffer, sizeof(pidBuffer), "%lu", ctx.sampleInfo[i].pid);
        } else {
            snprintf(pidBuffer, sizeof(pidBuffer), "(task)");
        }

        printf("%-40s %6.1f %10d %10.1f %6s\n",
               ctx.sampleInfo[i].nameBuffer,
               cpu,
               ctx.sampleInfo[i].priority,
               ctx.sampleInfo[i].stackUsage,
               pidBuffer);
    }

    if (ctx.debugMode) {
        ITimer->ReadEClock(&finish.un.clockVal);

        printf("\nDEBUG: data processing time %g us, longest interrupt %g us\n",
            TicksToMicros(finish.un.ticks - start.un.ticks), TicksToMicros(longestInterrupt));
    }
}

void ShellLoop(void)
{
    const uint32 signalMask = 1L << ctx.timerSignal;

    while (ctx.running) {
        const uint32 wait = IExec->Wait(signalMask | SIGBREAKF_CTRL_C);

        if (wait & signalMask) {
            ShowResults();
        }

        if (wait & SIGBREAKF_CTRL_C) {
            ctx.running = FALSE;
        }
    }
}

