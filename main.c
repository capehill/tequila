#include "timer.h"
#include "gui.h"
#include "profiler.h"
#include "version.h"
#include "symbols.h"
#include "common.h"

#include <stdio.h>

#include <proto/dos.h>
#include <proto/exec.h>

static const char* const version __attribute__((used)) = "$VER: " VERSION_STRING " " DATE_STRING;
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

static void parseArgs(void)
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

static BOOL initContext()
{
    ctx.mainSig = -1;

    parseArgs();

    ctx.interrupt = (struct Interrupt *) IExec->AllocSysObjectTags(ASOT_INTERRUPT,
        ASOINTR_Code, interruptCode,
        TAG_DONE);

    if (!ctx.interrupt) {
        puts("Couldn't allocate interrupt");
        return FALSE;
    }

    ctx.sampleBuffers[0] = allocMem(ctx.interval * ctx.samples * sizeof(Sample));
    ctx.sampleBuffers[1] = allocMem(ctx.interval * ctx.samples * sizeof(Sample));

    if (!ctx.sampleBuffers[0] || !ctx.sampleBuffers[1]) {
        puts("Failed to allocate sample buffers");
        return FALSE;
    }

    ctx.sampleInfo = allocMem(ctx.interval * ctx.samples * sizeof(SampleInfo));

    if (!ctx.sampleInfo) {
        puts("Failed to allocate sample info buffer");
        return FALSE;
    }

    if (ctx.profile) {
        ctx.maxAddresses = 30 * ctx.samples;
        ctx.addresses = allocMem(ctx.maxAddresses * sizeof(ULONG *));

        if (!ctx.addresses) {
            puts("Failed to allocate address buffer");
            return FALSE;
        }
    }

    ctx.back = ctx.sampleBuffers[0];
    ctx.front = NULL;

    ctx.nameBuffer = allocMem(4 * NAME_LEN);
    ctx.cliNameBuffer = allocMem(4 * NAME_LEN);

    if (!ctx.nameBuffer || !ctx.cliNameBuffer) {
        return FALSE;
    }

    ctx.mainTask = IExec->FindTask(NULL);
    ctx.mainSig = IExec->AllocSignal(-1);

    if (ctx.mainSig == -1) {
        puts("Couldn't allocate signal");
        return FALSE;
    }

    timerInit(&ctx.sampler, ctx.interrupt);

    ctx.interrupt->is_Node.ln_Name = (char *)"Tequila";
    ctx.running = TRUE;

    timerStart(ctx.sampler.request, ctx.period);

    return TRUE;
}

static void cleanupContext()
{
    if (ctx.mainSig != -1) {
        IExec->FreeSignal(ctx.mainSig);
        ctx.mainSig = -1;
    }

    timerQuit(&ctx.sampler);

    freeMem(ctx.nameBuffer);
    freeMem(ctx.cliNameBuffer);

    ctx.nameBuffer = ctx.cliNameBuffer = NULL;

    freeMem(ctx.sampleInfo);
    freeMem(ctx.sampleBuffers[0]);
    freeMem(ctx.sampleBuffers[1]);

    ctx.sampleInfo = NULL;
    ctx.sampleBuffers[0] = ctx.sampleBuffers[1] = NULL;

    if (ctx.profile) {
        freeMem(ctx.addresses);
        ctx.addresses = NULL;
    }

    if (ctx.interrupt) {
        IExec->FreeSysObject(ASOT_INTERRUPT, ctx.interrupt);
        ctx.interrupt = NULL;
    }
}

int main(int /*argc*/, char** /*argv*/)
{
    if (initContext()) {
        if (ctx.gui) {
            guiLoop();
        } else {
            shellLoop();
        }

        if (ctx.profile) {
            showSymbols();
        }

        timerWait(1000000);
    }

    cleanupContext();

    return 0;
}

