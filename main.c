#include "timer.h"
#include "gui.h"
#include "profiler.h"
#include "version.h"
#include "symbols.h"
#include "common.h"

#include <stdio.h>

#include <proto/dos.h>
#include <proto/exec.h>

static const char* const version __attribute__((used)) = "$VER: " VERSION_STRING DATE_STRING;
static const char* stackCookie __attribute__((used)) = "$STACK:64000";

typedef struct Params {
    LONG* samples;
    LONG* interval;
    LONG debug;
    LONG profile;
    LONG gui;
} Params;

static Params params = { NULL, NULL, 0, 0, 0 };

Context ctx;

static void ParseArgs(void)
{
    const char* const pattern = "SAMPLES/N,INTERVAL/N,DEBUG/S,PROFILE/S,GUI/S";

    struct RDArgs* result = IDOS->ReadArgs(pattern, (int32 *)&params, NULL);

    if (result) {
        if (params.samples) {
            ctx.samples = *params.samples;
        }

        if (params.interval) {
            ctx.interval = *params.interval;
        }

        ctx.debugMode = params.debug;
        ctx.profile = params.profile;
        ctx.gui = params.gui;

        IDOS->FreeArgs(result);
    } else {
        printf("Supported arguments: %s\n", pattern);
    }

    if (ctx.samples < 99) {
        puts("Min samples (freq) 99 Hz");
        ctx.samples = 99;
    } else if (ctx.samples > 10000) {
        puts("Max samples (freq) 10000 Hz");
        ctx.samples = 10000;
    }

    ctx.period = 1000000 / ctx.samples;

    if (ctx.interval < 1) {
        puts("Min interval 1");
        ctx.interval = 1;
    } else if (ctx.interval > 5) {
        puts("Max interval 5");
        ctx.interval = 5;
    }
}

static BOOL InitContext(const int /*argc*/, char* /*argv*/[])
{
    ctx.timerSignal = -1;
    ctx.lastSignal = -1;
    ctx.samples = 999;
    ctx.interval = 1;

    ParseArgs();

    ctx.interrupt = (struct Interrupt *) IExec->AllocSysObjectTags(ASOT_INTERRUPT,
        ASOINTR_Code, InterruptCode,
        TAG_DONE);

    if (!ctx.interrupt) {
        puts("Failed to allocate interrupt");
        return FALSE;
    }

    ctx.sampleBuffers[0] = AllocateMemory(ctx.interval * ctx.samples * sizeof(Sample));
    ctx.sampleBuffers[1] = AllocateMemory(ctx.interval * ctx.samples * sizeof(Sample));

    if (!ctx.sampleBuffers[0] || !ctx.sampleBuffers[1]) {
        puts("Failed to allocate sample buffers");
        return FALSE;
    }

    ctx.sampleInfo = AllocateMemory(ctx.interval * ctx.samples * sizeof(SampleInfo));

    if (!ctx.sampleInfo) {
        puts("Failed to allocate sample info buffer");
        return FALSE;
    }

    if (ctx.profile) {
        ctx.maxAddresses = 30 * ctx.samples;
        ctx.addresses = AllocateMemory(ctx.maxAddresses * sizeof(ULONG *));

        if (!ctx.addresses) {
            puts("Failed to allocate address buffer");
            return FALSE;
        }
    }

    ctx.back = ctx.sampleBuffers[0];
    ctx.front = NULL;

    ctx.nameBuffer = AllocateMemory(4 * NAME_LEN);
    ctx.cliNameBuffer = AllocateMemory(4 * NAME_LEN);

    if (!ctx.nameBuffer || !ctx.cliNameBuffer) {
        return FALSE;
    }

    ctx.mainTask = IExec->FindTask(NULL);
    ctx.timerSignal = IExec->AllocSignal(-1);
    ctx.lastSignal = IExec->AllocSignal(-1);

    if (ctx.timerSignal == -1 || ctx.lastSignal == -1) {
        puts("Failed to allocate signal");
        return FALSE;
    }

    TimerInit(&ctx.sampler, ctx.interrupt);

    ctx.interrupt->is_Node.ln_Name = (char *)"Tequila";
    ctx.running = TRUE;

    TimerStart(ctx.sampler.request, ctx.period);

    return TRUE;
}

static void CleanupContext()
{
    if (ctx.timerSignal != -1) {
        IExec->FreeSignal(ctx.timerSignal);
        ctx.timerSignal = -1;
    }

    if (ctx.lastSignal != -1) {
        IExec->FreeSignal(ctx.lastSignal);
        ctx.lastSignal = -1;
    }

    TimerQuit(&ctx.sampler);

    FreeMemory(ctx.nameBuffer);
    FreeMemory(ctx.cliNameBuffer);

    ctx.nameBuffer = ctx.cliNameBuffer = NULL;

    FreeMemory(ctx.sampleInfo);
    FreeMemory(ctx.sampleBuffers[0]);
    FreeMemory(ctx.sampleBuffers[1]);

    ctx.sampleInfo = NULL;
    ctx.sampleBuffers[0] = ctx.sampleBuffers[1] = NULL;

    if (ctx.profile) {
        FreeMemory(ctx.addresses);
        ctx.addresses = NULL;
    }

    if (ctx.interrupt) {
        IExec->FreeSysObject(ASOT_INTERRUPT, ctx.interrupt);
        ctx.interrupt = NULL;
    }
}

int main(int argc, char* argv[])
{
    if (InitContext(argc, argv)) {
        if (ctx.gui) {
            GuiLoop();
        } else {
            ShellLoop();
        }

        const uint32 signalMask = 1L << ctx.lastSignal;
        const uint32 signal = IExec->Wait(signalMask);
        if (ctx.debugMode && signal & signalMask) {
            puts("Last signal received");
        }

        if (ctx.profile) {
            ShowSymbols();
        }
    }

    CleanupContext();

    return 0;
}

