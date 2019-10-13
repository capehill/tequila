#include "common.h"
#include "timer.h"

#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const version __attribute__((used)) = "\0$VER: Tequila 0.1 (13.10.2019)";
static const char* stackCookie __attribute__((used)) = "$STACK:64000";

typedef struct Sample {
    struct Task* task;
} Sample;

typedef struct SampleInfo {
    char nameBuffer[NAME_LEN];
    struct Task* task;
    unsigned count;
    BYTE priority;
    float stackUsage;
} SampleInfo;

typedef struct TaskInfo {
    BYTE priority;
    float stackUsage;
} TaskInfo;

static char* nameBuffer;
static char* cliNameBuffer;

static TaskInfo taskInfo;

static Sample* samples[2];
static Sample* front;
static Sample* back;

static BOOL running = FALSE;

static BYTE mainSig = -1;
static struct Task* mainTask;

static TimerContext sampler;

typedef struct Params {
    LONG* samples;
    LONG* interval;
    LONG debug;
} Params;

static Params params = { NULL, NULL, 0 };
static ULONG period;
static ULONG freq;
static ULONG interval;

static BOOL debugMode = FALSE;

static uint64 longest;

static void interruptCode()
{
    struct MyClock start, finish;

    if (debugMode) {
        ITimer->ReadEClock(&start.un.clockVal);
    }

    struct ExecBase* sysbase = (struct ExecBase *)SysBase;
    struct Task* task = sysbase->ThisTask;
    static unsigned counter = 0;

    back[counter].task = task;

    if (++counter >= (freq * interval)) {
        static int flip = 0;

        counter = 0;
        front = samples[flip];
        flip ^= 1; // TODO: if main process doesn't get CPU, there might be glitches
        back = samples[flip];
        //IExec->DebugPrintF("Signal %d -> main\n", mainSig);
        IExec->Signal(mainTask, 1L << mainSig);
    }

    struct TimeRequest *request = (struct TimeRequest *)IExec->GetMsg(sampler.port);

    if (request && running) {
        timerStart(request, period);
    }

    if (debugMode) {
        ITimer->ReadEClock(&finish.un.clockVal);

        const uint64 duration = finish.un.ticks - start.un.ticks;
        if (duration > longest) {
            longest = duration;
        }
    }
}

static void getCliName(struct Task* task)
{
    cliNameBuffer[0] = '\0';

    if (IS_PROCESS(task)) {
        struct CommandLineInterface* cli = (struct CommandLineInterface *)BADDR(((struct Process *)task)->pr_CLI);
        if (cli) {
            const char* commandName = (const char *)BADDR(cli->cli_CommandName);
            if (commandName) {
                copyString(cliNameBuffer, " [", 2);

                // BSTR
                size_t len = *(UBYTE *)commandName;
                copyString(cliNameBuffer + stringLen(cliNameBuffer), commandName + 1, len);
                copyString(cliNameBuffer + stringLen(cliNameBuffer), "]", 1);
            }
        }
    }
}

static BOOL traverse(struct List* list, struct Task* target)
{
    for (struct Node* node = IExec->GetHead(list); node; node = IExec->GetSucc(node)) {
        struct Task* task = (struct Task *)node;

        if (task == target) {
            getCliName(task);

            copyString(nameBuffer, node->ln_Name, stringLen(node->ln_Name));

            if (stringLen(cliNameBuffer) > 0) {
                copyString(nameBuffer + stringLen(nameBuffer), cliNameBuffer, stringLen(cliNameBuffer));
            }

            taskInfo.priority = ((struct Node *)task)->ln_Pri;

            const float totalStack = task->tc_SPUpper - task->tc_SPLower;
            const float usedStack = task->tc_SPUpper - task->tc_SPReg;

            taskInfo.stackUsage = 100.0f * usedStack / totalStack;
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL traverseLists(struct Task* task)
{
    struct ExecBase* eb = (struct ExecBase *)SysBase;

    IExec->Disable();

    BOOL found = traverse(&eb->TaskReady, task);

    if (!found) {
        found = traverse(&eb->TaskWait, task);
    }

    IExec->Enable();

    return found;
}

static SampleInfo findTaskData(struct Task* task)
{
    SampleInfo info;

    nameBuffer[0] = '\0';
    taskInfo.priority = 0;
    taskInfo.stackUsage = 0.0f;

    BOOL found = traverseLists(task);

    if (!found) {
        if (task == mainTask) {
            snprintf(info.nameBuffer, NAME_LEN, "* Tequila (this process)");
            info.priority = ((struct Node *)task)->ln_Pri;
        } else {
            snprintf(info.nameBuffer, NAME_LEN, "Unknown task %p", task);
            info.priority = 0;
        }
    } else {
        snprintf(info.nameBuffer, NAME_LEN, nameBuffer);
        info.priority = taskInfo.priority;
    }

    info.count = 1;
    info.task = task;
    info.stackUsage = taskInfo.stackUsage;

    return info;
}

static int comparison(const void* first, const void* second)
{
    const SampleInfo* a = first;
    const SampleInfo* b = second;

    if (a->count > b->count) return -1;
    if (a->count < b->count) return 1;

    return 0;
}

static size_t prepareResults(SampleInfo* results)
{
    size_t unique = 0;

    for (size_t sample = 0; sample < interval * freq; sample++) {
        struct Task* task = front[sample].task;

        BOOL found = FALSE;

        for (size_t i = 0; i < unique; i++) {
            if (results[i].task == task) {
                results[i].count++;
                //IExec->DebugPrintF("count %u for task %p\n", results[i].count, task);
                found = TRUE;
                break;
            }
        }

        if (!found) {
            results[unique] = findTaskData(task);
            unique++;
        }
    }

    qsort(results, unique, sizeof(SampleInfo), comparison);

    return unique;
}

static char* getCpuState(float usage)
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

static float getLoad(SampleInfo* results, size_t count)
{
    float idleCpu = 0.0f;

    for (size_t i = 0; i < count; i++) {
        if (strcmp(results[i].nameBuffer, "idle.task") == 0) {
            idleCpu = 100.0f * results[i].count / (freq * interval);
            break;
        }
    }

    return 100.0f - idleCpu;
}

static void showResults(SampleInfo* results)
{
    MyClock start, finish;

    if (debugMode) {
        ITimer->ReadEClock(&start.un.clockVal);
    }

    const size_t unique = prepareResults(results);

    const float usage = getLoad(results, unique);

    static unsigned round = 0;
	
    printf("%cc[[ Tequila ]] - Round # %u, frequency %lu Hz, interval %lu seconds, status [%s]\n",
        0x1B, round++, freq, interval, getCpuState(usage));

    printf("%-40s %6s %10s %10s\n", "Task name:", "CPU %", "Priority", "Stack %");

    for (size_t i = 0; i < unique; i++) {
        const float cpu = 100.0f * results[i].count / (freq * interval);

        printf("%-40s %6.2f %10d %10.2f\n", results[i].nameBuffer, cpu, results[i].priority, results[i].stackUsage);
    }

    if (debugMode) {
        ITimer->ReadEClock(&finish.un.clockVal);

        printf("\nDEBUG: data processing time %g us, longest interrupt %g us\n",
            ticksToMicros(finish.un.ticks - start.un.ticks), ticksToMicros(longest));
    }
}

static void loop()
{
    const uint32 signalMask = 1L << mainSig;

    SampleInfo* results = allocMem(sizeof(SampleInfo) * freq * interval);

    if (!results) {
        puts("Failed to allocate memory");
        return;
    }

    while (running) {
        const uint32 wait = IExec->Wait(signalMask | SIGBREAKF_CTRL_C);

        if (wait & signalMask) {
            showResults(results);
        }

        if (wait & SIGBREAKF_CTRL_C) {
            puts("...Adios!");
            running = FALSE;
        }
    }

    freeMem(results);
}

static void parseArgs(void)
{
    const char* const pattern = "SAMPLES/N,INTERVAL/N,DEBUG/S";

    struct RDArgs* result = IDOS->ReadArgs(pattern, (int32 *)&params, NULL);

    if (result) {
        if (params.samples) {
            freq = *params.samples;
        }

        if (params.interval) {
            interval = *params.interval;
        }

        debugMode = params.debug;

        IDOS->FreeArgs(result);
    } else {
        printf("Supported arguments: %s\n", pattern);
    }

    if (freq < 99) {
        puts("Min freq 99 Hz");
        freq = 99;
    } else if (freq > 10000) {
        puts("Max freq 10000 Hz");
        freq = 10000;
    }

    period = 1000000 / freq;

    if (interval < 1) {
        puts("Min interval 1");
        interval = 1;
    } else if (interval > 5) {
        puts("Max interval 5");
        interval = 5;
    }
}

int main()
{
    parseArgs();

    struct Interrupt* interrupt = (struct Interrupt *) IExec->AllocSysObjectTags(ASOT_INTERRUPT,
        ASOINTR_Code, interruptCode,
        TAG_DONE);

    if (!interrupt) {
        puts("Couldn't allocate interrupt");
        goto quit;
    }

    samples[0] = allocMem(interval * freq * sizeof(Sample));
    samples[1] = allocMem(interval * freq * sizeof(Sample));

    if (!samples[0] || !samples[1]) {
        goto quit;
    }

    back = samples[0];
    front = NULL;

    nameBuffer = allocMem(4 * NAME_LEN);
    cliNameBuffer = allocMem(4 * NAME_LEN);

    if (!nameBuffer || !cliNameBuffer) {
        goto quit;
    }

    mainTask = IExec->FindTask(NULL);
    mainSig = IExec->AllocSignal(-1);

    if (mainSig == -1) {
        puts("Couldn't allocate signal");
        goto quit;
    }

    timerInit(&sampler, interrupt);

    interrupt->is_Node.ln_Name = (char *)"Tequila";

    running = TRUE;

    timerStart(sampler.request, period);

    loop();

    timerWait(1000000);

quit:

    if (mainSig != -1) {
        IExec->FreeSignal(mainSig);
        mainSig = -1;
    }

    timerQuit(&sampler);

    freeMem(nameBuffer);
    freeMem(cliNameBuffer);

    nameBuffer = cliNameBuffer = NULL;

    freeMem(samples[0]);
    freeMem(samples[1]);

    samples[0] = samples[1] = NULL;

    if (interrupt) {
        IExec->FreeSysObject(ASOT_INTERRUPT, interrupt);
        interrupt = NULL;
    }

    return 0;
}

