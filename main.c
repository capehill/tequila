#include "timer.h"
#include "gui.h"
#include "profiler.h"
#include "version.h"
#include "symbols.h"
#include "common.h"
#include "locale.h"

#include <stdio.h>
#include <stdlib.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>

#include <workbench/startup.h>

static const char* const version __attribute__((used)) = "$VER: " VERSION_STRING DATE_STRING;
static const char* stackCookie __attribute__((used)) = "$STACK:64000";

typedef struct Params {
    LONG* samples;
    LONG* interval;
    LONG debug;
    LONG profile;
    LONG gui;
    LONG customRendering;
} Params;

static Params params = { NULL, NULL, 0, 0, 0, 0};

Context ctx;

static void ParseArgs(void)
{
    const char* const pattern = "SAMPLES/N,INTERVAL/N,DEBUG/S,PROFILE/S,GUI/S,CUSTOMRENDERING/S";

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
        ctx.customRendering = params.customRendering;

        IDOS->FreeArgs(result);
    } else {
        printf("Supported arguments: %s\n", pattern);
    }
}

static void ValidateArgs(void)
{
    if (ctx.samples < 99) {
        puts("Min samples (freq) 99 Hz");
        ctx.samples = 99;
    } else if (ctx.samples > 10000) {
        puts("Max samples (freq) 10000 Hz");
        ctx.samples = 10000;
    }

    ctx.period = 1000000 / ctx.samples;

    if (ctx.interval < 1) {
        puts("Min interval 1 second");
        ctx.interval = 1;
    } else if (ctx.interval > 5) {
        puts("Max interval 5 seconds");
        ctx.interval = 5;
    }
}

static int ToolTypeToNumber(struct DiskObject* diskObject, const char* const name)
{
    const char* const valueString = IIcon->FindToolType(diskObject->do_ToolTypes, name);
    if (valueString) {
        return atoi(valueString);
    }

    return 0;
}

static void ReadToolTypes(const char* const name)
{
    if (name) {
        struct DiskObject* diskObject = IIcon->GetDiskObject(name);
        if (diskObject) {
            ctx.samples = ToolTypeToNumber(diskObject, "SAMPLES");
            ctx.interval = ToolTypeToNumber(diskObject, "INTERVAL");
            ctx.debugMode = IIcon->FindToolType(diskObject->do_ToolTypes, "DEBUG") != NULL;
            ctx.profile = IIcon->FindToolType(diskObject->do_ToolTypes, "PROFILE") != NULL;
            ctx.gui = IIcon->FindToolType(diskObject->do_ToolTypes, "GUI") != NULL;
            ctx.customRendering = IIcon->FindToolType(diskObject->do_ToolTypes, "CUSTOMRENDERING") != NULL;
            IIcon->FreeDiskObject(diskObject);
        }
    }
}

static BOOL InitContext(const int argc, char* argv[])
{
    ctx.timerSignal = -1;
    ctx.lastSignal = -1;
    ctx.samples = 999;
    ctx.interval = 1;

    if (argc > 0) {
        ParseArgs();
        ReadToolTypes(argv[0]);
    } else {
        struct WBStartup* startup = (struct WBStartup *)argv;
        struct WBArg* args = startup->sm_ArgList;

        ReadToolTypes(args->wa_Name);
    }

    ValidateArgs();

    ctx.interrupt = (struct Interrupt *) IExec->AllocSysObjectTags(ASOT_INTERRUPT,
        ASOINTR_Code, InterruptCode,
        ASOINTR_Name, "Tequila timer interrupt",
        ASOINTR_Pri, 0, // TODO: is 0 value OK?
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
    signal(SIGINT, SIG_IGN);

    LocaleInit();

    if (InitContext(argc, argv)) {
        if (ctx.gui) {
            GuiLoop();
        } else {
            ShellLoop();
        }

        const uint32 signalMask = 1L << ctx.lastSignal;
        const uint32 signal = IExec->Wait(signalMask);
        if (ctx.debugMode && (signal & signalMask)) {
            puts("Last signal received");
        }

        if (ctx.profile) {
            ShowSymbols();
        }
    }

    CleanupContext();

    LocaleQuit();

    return 0;
}

