#include "common.h"
#include "timer.h"
#include "symbols.h"
#include "profiler.h"

#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64 longest;

void InterruptCode(void)
{
    BOOL quit = FALSE;
    struct MyClock start, finish;

    if (ctx.debugMode) {
        ITimer->ReadEClock(&start.un.clockVal);
    }

    //IExec->Disable();

    struct ExecBase* sysbase = (struct ExecBase *)SysBase;
    struct Task* task = sysbase->ThisTask;
    static unsigned counter = 0;

    ctx.back[counter].task = task;

    if (ctx.profile) {
        static unsigned addressCounter = 0;

        // TODO: Disable() needed?
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

    //IExec->Enable();

    if (++counter >= (ctx.samples * ctx.interval)) {
        static int flip = 0;

        counter = 0;
        ctx.front = ctx.sampleBuffers[flip];
        flip ^= 1; // TODO: if main process doesn't get CPU, there might be glitches
        ctx.back = ctx.sampleBuffers[flip];
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
        if (duration > longest) {
            longest = duration;
        }
    }

    if (quit) {
        IExec->Signal(ctx.mainTask, 1L << ctx.lastSignal);
    }
}

static void GetCliName(struct Task* task)
{
    ctx.cliNameBuffer[0] = '\0';

    if (IS_PROCESS(task)) {
        struct Process* process = (struct Process *)task;
        ctx.taskInfo.pid = process->pr_ProcessID;
        struct CommandLineInterface* cli = (struct CommandLineInterface *)BADDR(((struct Process *)task)->pr_CLI);
        if (cli) {
            const char* commandName = (const char *)BADDR(cli->cli_CommandName);
            if (commandName) {
                CopyString(ctx.cliNameBuffer, " [", 2);

                // BSTR
                size_t len = *(UBYTE *)commandName;
                CopyString(ctx.cliNameBuffer + StringLen(ctx.cliNameBuffer), commandName + 1, len);
                CopyString(ctx.cliNameBuffer + StringLen(ctx.cliNameBuffer), "]", 1);
            }
        }
    }
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
            GetCliName(task);

            CopyString(ctx.nameBuffer, node->ln_Name, StringLen(node->ln_Name));

            if (StringLen(ctx.cliNameBuffer) > 0) {
                CopyString(ctx.nameBuffer + StringLen(ctx.nameBuffer), ctx.cliNameBuffer, StringLen(ctx.cliNameBuffer));
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

    IExec->Disable();

    tasks += GetTaskCount(&eb->TaskReady);
    tasks += GetTaskCount(&eb->TaskWait);

    IExec->Enable();

    return tasks;
}

static BOOL TraverseLists(struct Task* task)
{
    struct ExecBase* eb = (struct ExecBase *)SysBase;

    IExec->Disable();

    BOOL found = Traverse(&eb->TaskReady, task);

    if (!found) {
        found = Traverse(&eb->TaskWait, task);
    }

    IExec->Enable();

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
            snprintf(info.nameBuffer, NAME_LEN, "* Tequila (this task)");
            ctx.taskInfo.stackUsage = GetStackUsage(task);
            ctx.taskInfo.pid = ((struct Process *)task)->pr_ProcessID;
            ctx.taskInfo.priority = ((struct Node *)task)->ln_Pri;
        } else {
            /* Could be some removed task */
            snprintf(info.nameBuffer, NAME_LEN, "Unknown task %p", task);
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

    for (size_t sample = 0; sample < ctx.interval * ctx.samples; sample++) {
        struct Task* task = ctx.front[sample].task;

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

    return unique;
}
/*
static char* GetCpuState(const float usage)
{
    if (usage >= 90.0f) {
        return "BUSY";
    } else if (usage >= 50.0f) {
        return "HEAVY LOAD";
    } else if (usage >= 5.0f) {
        return "SOME LOAD";
    }

    return "IDLING";
}
*/
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
                idleCpu += 100.0f * ctx.sampleInfo[i].count / (ctx.samples * ctx.interval);
            }
        }
    }

    return idleCpu;
}

/*
static float GetLoad(const size_t count)
{
    return 100.0f - GetIdleCpu(count);
}
*/

static void ShowResults(void)
{
    MyClock start, finish;

    if (ctx.debugMode) {
        ITimer->ReadEClock(&start.un.clockVal);
    }

    const size_t unique = PrepareResults();
    //const float usage = GetLoad(unique);

    printf("%cc[[ Tequila ]] - frequency %lu Hz, idle %3.2f%%, tasks %u. Uptime %s\n",
        0x1B, ctx.samples, GetIdleCpu(unique), GetTotalTaskCount(), GetUptimeString());

    printf("%-40s %6s %10s %10s %6s\n", "Task name:", "CPU %", "Priority", "Stack %", "PID");

    for (size_t i = 0; i < unique; i++) {
        const float cpu = 100.0f * ctx.sampleInfo[i].count / (ctx.samples * ctx.interval);

        printf("%-40s %6.2f %10d %10.2f %6lu\n", ctx.sampleInfo[i].nameBuffer, cpu, ctx.sampleInfo[i].priority, ctx.sampleInfo[i].stackUsage, ctx.sampleInfo[i].pid);
    }

    if (ctx.debugMode) {
        ITimer->ReadEClock(&finish.un.clockVal);

        printf("\nDEBUG: data processing time %g us, longest interrupt %g us\n",
            TicksToMicros(finish.un.ticks - start.un.ticks), TicksToMicros(longest));
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

