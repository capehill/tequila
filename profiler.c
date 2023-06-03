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

static void CopyTaskData(struct Task* task, SampleInfo* info)
{
    struct Node* node = (struct Node *)task;
    const size_t cliNameLen = CopyProcessData(task, info);
    const size_t nameLen = strlen(node->ln_Name);

    strlcpy(info->nameBuffer, node->ln_Name, NAME_LEN);

    if (cliNameLen > 0) {
        strlcpy(info->nameBuffer + nameLen, ctx.cliNameBuffer, NAME_LEN - nameLen);
    }

    info->stackUsage = GetStackUsage(task);
    info->priority = node->ln_Pri;
}

static SampleInfo InitializeTaskData(struct Task* task)
{
    SampleInfo info;

    info.nameBuffer[0] = '\0';
    info.stackUsage = 0.0f;
    info.pid = 0;
    info.priority = 0;
    info.task = task;
    info.count = 1;

    if (task == ctx.mainTask) {
        // TODO: can we call GetString in interrupt?
        snprintf(info.nameBuffer, NAME_LEN, "* Tequila" /*, GetString(MSG_THIS_TASK)*/);
        info.stackUsage = GetStackUsage(task);
        info.pid = ((struct Process *)task)->pr_ProcessID;
        info.priority = ((struct Node *)task)->ln_Pri;
    } else {
        CopyTaskData(task, &info);
    }

    return info;
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
    static unsigned counter = 0;

    BOOL found = FALSE;

    uint32 i;

    for (i = 0 ; i < ctx.back->uniqueTasks; i++) {
        if (task == ctx.back->sampleInfoBuffer[i].task) {
            ctx.back->sampleInfoBuffer[i].count++;
            found = TRUE;
            break;
        }
    }

    if (!found) {
        if (i < MAX_TASKS) {
            ctx.back->sampleInfoBuffer[i] = InitializeTaskData(task);
            ctx.back->uniqueTasks++;
        } else {
            ctx.back->overflowTasks++;
        }
    }

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

    if (++counter >= (ctx.samples * ctx.interval)) {
        static int flip = 0;

        counter = 0;
        ctx.front = &ctx.sampleData[flip];
        flip ^= 1; // TODO: if main process doesn't get CPU, there might be glitches
        ctx.back = &ctx.sampleData[flip];
        ctx.back->forbidCount = 0;
        ctx.back->uniqueTasks = 0;
        ctx.back->overflowTasks = 0;

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
    qsort(ctx.front->sampleInfoBuffer, ctx.front->uniqueTasks, sizeof(SampleInfo), Comparison);

    const ULONG dispCount = ((struct ExecBase *)SysBase)->DispCount;

    ctx.taskSwitchesPerSecond = (dispCount - ctx.lastDispCount) / ctx.interval;
    ctx.lastDispCount = dispCount;

    return ctx.front->uniqueTasks;
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
            if (strcmp(ctx.front->sampleInfoBuffer[i].nameBuffer, knownIdleTaskNames[n]) == 0) {
                idleCpu += 100.0f * ctx.front->sampleInfoBuffer[i].count / (ctx.samples * ctx.interval);
            }
        }
    }

    return idleCpu;
}

float GetForbidCpu(void)
{
    return 100.0f * ctx.front->forbidCount / (ctx.samples * ctx.interval);
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
        const float cpu = 100.0f * ctx.front->sampleInfoBuffer[i].count / (ctx.samples * ctx.interval);

        static char pidBuffer[16];

        if (ctx.front->sampleInfoBuffer[i].pid > 0) {
            snprintf(pidBuffer, sizeof(pidBuffer), "%lu", ctx.front->sampleInfoBuffer[i].pid);
        } else {
            snprintf(pidBuffer, sizeof(pidBuffer), "(task)");
        }

        printf("%-40s %6.1f %10d %10.1f %6s\n",
               ctx.front->sampleInfoBuffer[i].nameBuffer,
               cpu,
               ctx.front->sampleInfoBuffer[i].priority,
               ctx.front->sampleInfoBuffer[i].stackUsage,
               pidBuffer);
    }

    if (ctx.debugMode) {
        ITimer->ReadEClock(&finish.un.clockVal);

        const uint64 ticks = finish.un.ticks - start.un.ticks;
        if (ticks > ctx.longestDisplayUpdate) {
            ctx.longestDisplayUpdate = ticks;
        }

        printf("\nDisplay update %g us (longest %g us), longest interrupt %g us\n",
            TicksToMicros(ticks),
            TicksToMicros(ctx.longestDisplayUpdate),
            TicksToMicros(ctx.longestInterrupt));
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

