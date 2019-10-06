#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME_LEN 256

static const char* const version __attribute__((used)) = "\0$VER: Tequila 0.1 (06.10.2019)";
static const char* stackCookie __attribute__((used)) = "$STACK:64000";

typedef struct TimerContext {
    struct MsgPort* port;
    struct TimeRequest* request;
    BYTE device;
} TimerContext;

typedef struct Sample {
    struct Task* task;
} Sample;

typedef struct SampleInfo {
    char nameBuffer[NAME_LEN];
    struct Task* task;
    unsigned count;
    BYTE priority;
} SampleInfo;

static Sample* samples[2];
static Sample* front;
static Sample* back;

static BOOL running = FALSE;

static BYTE mainSig = -1;
static struct Task* mainTask;

static TimerContext sampler;
static TimerContext endTimer;

typedef struct Params {
    LONG* samples;
} Params;

static Params params = { NULL };
static ULONG period;
static ULONG freq;

static APTR allocMem(size_t size)
{
    return IExec->AllocVecTags(size,
        AVT_ClearWithValue, 0,
        TAG_DONE);
}

static void freeMem(APTR address)
{
    if (!address) {
        IExec->DebugPrintF("%s: nullptr\n", __func__);
    }

    IExec->FreeVec(address);
}

static void timerStart(struct TimeRequest * request, ULONG micros)
{
    if (!request) {
        IExec->DebugPrintF("TimeRequest nullptr\n");
        return;
    }

    if (micros == 0) {
        IExec->DebugPrintF("Timer period 0");
        return;
    }

    request->Request.io_Command = TR_ADDREQUEST;
    request->Time.Seconds = 0;
    request->Time.Microseconds = micros;

    IExec->BeginIO((struct IORequest *)request);
}

static void timerQuit(TimerContext * ctx)
{
    if (!ctx) {
        IExec->DebugPrintF("%s: timer context nullptr\n", __func__);
        return;
    }

    if (ctx->request) {
        IExec->CloseDevice((struct IORequest *)ctx->request);
        IExec->FreeSysObject(ASOT_IOREQUEST, ctx->request);
        ctx->request = NULL;
        ctx->device = -1;
    }

    if (ctx->port) {
        IExec->FreeSysObject(ASOT_PORT, ctx->port);
        ctx->port = NULL;
    }
}

static BOOL timerInit(TimerContext * ctx, struct Interrupt * interrupt)
{
    if (!ctx) {
        IExec->DebugPrintF("%s: timer context nullptr\n", __func__);
        return FALSE;
    }

    ctx->device = -1;
    ctx->port = NULL;
    ctx->request = NULL;

    if (interrupt) {
        ctx->port = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT,
            ASOPORT_Signal, FALSE,
            ASOPORT_Action, PA_SOFTINT,
            ASOPORT_Target, interrupt,
            TAG_DONE);
    } else {
        ctx->port = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT,
            ASOPORT_Name, "Timer port",
            TAG_DONE);
    }

    if (!ctx->port) {
        puts("Failed to allocate timer port");
        goto clean;
    }

    ctx->request = (struct TimeRequest *)IExec->AllocSysObjectTags(ASOT_IOREQUEST,
        ASOIOR_Size, sizeof(struct TimeRequest),
        ASOIOR_ReplyPort, ctx->port,
        TAG_DONE);

    if (!ctx->request) {
        puts("Failed to allocate TimeRequest");
        goto clean;
    }

    ctx->device = IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)ctx->request, 0);

    if (ctx->device) {
        puts("Failed to open timer.device");
        goto clean;
    }

    return TRUE;

clean:
    timerQuit(ctx);

    return FALSE;
}

static void timerWait(TimerContext * ctx, ULONG micros)
{
     if (!ctx) {
         IExec->DebugPrintF("%s: timer context nullptr\n", __func__);
         return;
     }

    timerStart(ctx->request, micros);

    const uint32 timerSig = 1L << ctx->port->mp_SigBit;

    uint32 wait;
    while ((wait = IExec->Wait(timerSig | SIGBREAKF_CTRL_C))) {
        if (wait & timerSig) {
            puts("Timer finish");
            break;
        }

        //puts("Stop pressing CTRL-C :)");
    }
}

static void code()
{
    struct ExecBase *sysbase = (struct ExecBase *)SysBase;
    struct Task* task = sysbase->ThisTask;
    static int flip = 0;
    static unsigned counter = 0;

    back[counter].task = task;

    if (++counter >= freq) {
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
}

static void copyString(char* const to, const char* const from, size_t len)
{
    if (to && from && len > 0) {
        for (size_t i = 0; i < len; i++) {
            to[i] = from[i];
        }
        to[len] = '\0';
    }
}

static size_t stringLen(const char* str)
{
    if (!str) {
        IExec->DebugPrintF("%s - nullptr\n", __func__);
        return 0;
    }

    size_t len = 0;

    while (*str++) {
        ++len;
    }

    return len;
}

static void getCliName(struct Task *task, char * nameBuffer)
{
    if (IS_PROCESS(task)) {
        struct CommandLineInterface *cli = (struct CommandLineInterface *)BADDR(((struct Process *)task)->pr_CLI);
        if (cli) {
            const char *commandName = (const char *)BADDR(cli->cli_CommandName);
            if (commandName) {
                copyString(nameBuffer, " [", 2);

                // BSTR
                size_t len = *(UBYTE*)commandName;
                copyString(nameBuffer + stringLen(nameBuffer), commandName + 1, len);
                copyString(nameBuffer + stringLen(nameBuffer), "]", 1);
            }
        }
    }
}

static BOOL traverse(struct List *list, struct Task *target, char * nameBuffer)
{
    for (struct Node *node = IExec->GetHead(list); node; node = IExec->GetSucc(node)) {
        struct Task *task = (struct Task*)node;

        if (task == target) {
            char cliNameBuffer[NAME_LEN] = { 0 };

            getCliName(task, cliNameBuffer);

            copyString(nameBuffer, node->ln_Name, stringLen(node->ln_Name));

            if (stringLen(cliNameBuffer) > 0) {
                copyString(nameBuffer + stringLen(nameBuffer), cliNameBuffer, stringLen(cliNameBuffer));
            }

            return TRUE;
        }
    }

    return FALSE;
}

static SampleInfo findTaskData(struct Task * task)
{
    struct ExecBase *eb = (struct ExecBase *)SysBase;
    SampleInfo info;

    char nameBuffer[NAME_LEN] = { 0 };

    IExec->Disable();

    BOOL result = traverse(&eb->TaskReady, task, nameBuffer);

    if (!result) {
        result = traverse(&eb->TaskWait, task, nameBuffer);
    }

    IExec->Enable();

    if (!result) {
        if (task == mainTask) {
            snprintf(info.nameBuffer, NAME_LEN, "Tequila (this process)");
            info.priority = ((struct Node *)task)->ln_Pri;
        } else {
            snprintf(info.nameBuffer, NAME_LEN, "Unknown task %p", task);
            info.priority = 0;
        }
    } else {
        snprintf(info.nameBuffer, NAME_LEN, nameBuffer);
        info.priority = ((struct Node *)task)->ln_Pri;
    }

    info.count = 1;
    info.task = task;

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

static void showResults(SampleInfo * results)
{
    size_t unique = 0;

    for (size_t sample = 0; sample < freq; sample++) {
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

    static unsigned round = 0;
	
    printf("%cc[[ Tequila ]] - Round # %d, frequency %lu Hz - [[ Control-C to quit ]]\n", 0x1B, round++, freq);
    printf("%-40s %6s %10s\n", "Task name:", "CPU %", "Priority");

    for (size_t i = 0; i < unique; i++) {
        const float cpu = 100.0f * results[i].count / freq;

        printf("%-40s %6.2f %10d\n", results[i].nameBuffer, cpu, results[i].priority);
    }
}

static void loop()
{
    const uint32 signalMask = 1L << mainSig;

    SampleInfo * results = allocMem(sizeof(SampleInfo) * freq);

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
            puts("User pressed CTRL_C");
            running = FALSE;
        }
    }

    freeMem(results);
}

static BOOL parseArgs(void)
{
    const char* const pattern = "SAMPLES/N";

    struct RDArgs *result = IDOS->ReadArgs(pattern, (int32 *)&params, NULL);

    if (result) {
        if (params.samples) {
            freq = *params.samples;
        }
    }

    if (freq < 99) {
        puts("Min freq 99 Hz");
        freq = 99;
    }

    if (freq > 10000) {
        puts("Max freq 10000 Hz");
        freq = 10000;
    }

    period = 1000000 / freq;

    return TRUE;
}

int main()
{
    parseArgs();

    struct Interrupt *interrupt = (struct Interrupt *) IExec->AllocSysObjectTags(ASOT_INTERRUPT,
        ASOINTR_Code, code,
        TAG_DONE);

    if (!interrupt) {
        puts("Couldn't allocate interrupt");
        goto quit;
    }

    samples[0] = allocMem(freq * sizeof(Sample));
    samples[1] = allocMem(freq * sizeof(Sample));

    if (!samples[0] || !samples[1]) {
        goto quit;
    }

    back = samples[0];
    front = NULL;

    mainTask = IExec->FindTask(NULL);
    mainSig = IExec->AllocSignal(-1);

    if (mainSig == -1) {
        puts("Couldn't allocate signal");
        goto quit;
    }

    timerInit(&sampler, interrupt);

    interrupt->is_Node.ln_Name = (char *)"Profiler";

    running = TRUE;

    timerStart(sampler.request, period);

    loop();

    if (timerInit(&endTimer, NULL)) {
        const ULONG oneSecond = 1000000;
        timerWait(&endTimer, oneSecond);
        timerQuit(&endTimer);
    } else {
        puts("Failed to create end timer");
    }

quit:

    if (mainSig != -1) {
        IExec->FreeSignal(mainSig);
        mainSig = -1;
    }

    timerQuit(&sampler);

    freeMem(samples[0]);
    freeMem(samples[1]);

    if (interrupt) {
        IExec->FreeSysObject(ASOT_INTERRUPT, interrupt);
        interrupt = NULL;
    }

    return 0;
}

