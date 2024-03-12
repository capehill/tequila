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
            ctx.samples = (ULONG)*params.samples;
        }

        if (params.interval) {
            ctx.interval = (ULONG)*params.interval;
        }

        ctx.debugMode = (BOOL)params.debug;
        ctx.profiling.enabled = (BOOL)params.profile;
        //ctx.profiling.task = IExec->FindTask((char *)params.profile); // TODO
        ctx.gui = (BOOL)params.gui;
        ctx.customRendering = (BOOL)params.customRendering;

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
            ctx.samples = (ULONG)ToolTypeToNumber(diskObject, "SAMPLES");
            ctx.interval = (ULONG)ToolTypeToNumber(diskObject, "INTERVAL");
            ctx.debugMode = IIcon->FindToolType(diskObject->do_ToolTypes, "DEBUG") != NULL;
            ctx.profiling.enabled = IIcon->FindToolType(diskObject->do_ToolTypes, "PROFILE") != NULL;
            //if (ctx.profiling.enabled) {
            //    ctx.profiling.task = IExec->FindTask(IIcon->FindToolType(diskObject->do_ToolTypes, "PROFILE"));
            //}
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
        //printf("Profile task %p\n", ctx.profiling.task);
    } else {
        struct WBStartup* startup = (struct WBStartup *)argv;
        struct WBArg* args = startup->sm_ArgList;

        ReadToolTypes(args->wa_Name);
    }

    ValidateArgs();

    ctx.totalSamples = ctx.interval * ctx.samples;

    ctx.interrupt = (struct Interrupt *) IExec->AllocSysObjectTags(ASOT_INTERRUPT,
        ASOINTR_Code, InterruptCode,
        ASOINTR_Name, "Tequila timer interrupt",
        ASOINTR_Pri, 0, // TODO: is 0 value OK?
        TAG_DONE);

    if (!ctx.interrupt) {
        puts("Failed to allocate interrupt");
        return FALSE;
    }

    ctx.sampleData[0].sampleBuffer = AllocateMemory(ctx.totalSamples * sizeof(Sample));
    ctx.sampleData[1].sampleBuffer = AllocateMemory(ctx.totalSamples * sizeof(Sample));

    if (!ctx.sampleData[0].sampleBuffer || !ctx.sampleData[1].sampleBuffer) {
        puts("Failed to allocate sample buffers");
        return FALSE;
    }

    if (ctx.profiling.enabled) {
        ctx.profiling.maxStackTraces = 30 * ctx.samples;
        ctx.profiling.samples = AllocateMemory(ctx.profiling.maxStackTraces * sizeof(StackTraceSample));

        if (!ctx.profiling.samples) {
            puts("Failed to allocate stack trace buffer");
            return FALSE;
        }
    }

    ctx.back = &ctx.sampleData[0];
    ctx.front = NULL;

    ctx.cliNameBuffer = AllocateMemory(NAME_LEN);

    if (!ctx.cliNameBuffer) {
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

    ctx.lastDispCount = ((struct ExecBase *)SysBase)->DispCount;

    ctx.symbolLookupWorkaroundNeeded = (SysBase->lib_Version <= 54) &&
                                       (SysBase->lib_Revision <= 46);

    if (ctx.debugMode && ctx.symbolLookupWorkaroundNeeded) {
        printf("exec.library version %d.%d: symbol lookup W/A activated\n", SysBase->lib_Version, SysBase->lib_Revision);
    }

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

    FreeMemory(ctx.cliNameBuffer);

    ctx.cliNameBuffer = NULL;

    FreeMemory(ctx.sampleData[0].sampleBuffer);
    FreeMemory(ctx.sampleData[1].sampleBuffer);

    ctx.sampleData[0].sampleBuffer = ctx.sampleData[1].sampleBuffer = NULL;

    if (ctx.profiling.enabled) {
        FreeMemory(ctx.profiling.samples);
        ctx.profiling.samples = NULL;
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

        if (ctx.profiling.enabled) {
            if (ctx.profiling.stackTraces) {
                ShowSymbols();
            } else {
                puts("No stack traces collected");
            }
        }
    }

    CleanupContext();

    LocaleQuit();

    return 0;
}

