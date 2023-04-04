#include "timer.h"

#include <stdio.h>

#include <proto/exec.h>
#include <dos/dos.h>

struct TimerIFace* ITimer;

static ULONG frequency;

static int users;

static void MyGetInterface(struct Device* device)
{
    if (!ITimer) {
        ITimer = (struct TimerIFace *) IExec->GetInterface((struct Library *)device, "main", 1, NULL);

        if (!ITimer) {
            puts("Failed to get ITimer");
            return;
        }
    }

    users++;
}

static void GetFrequency()
{
    if (!frequency) {
        struct EClockVal val;
        frequency = ITimer->ReadEClock(&val);
        IExec->DebugPrintF("Clock frequency %lu ticks/second\n", frequency);
    }
}

static void MyDropInterface() {
    if (--users <= 0 && ITimer) {
        IExec->DebugPrintF("ITimer user count %d, dropping it\n", users);
        IExec->DropInterface((struct Interface *) ITimer);
        ITimer = NULL;
    }
}

void TimerStart(struct TimeRequest* request, const ULONG micros)
{
    if (!request) {
        IExec->DebugPrintF("TimeRequest nullptr\n");
        return;
    }

    if (micros == 0) {
        IExec->DebugPrintF("Timer period 0\n");
        return;
    }

    request->Request.io_Command = TR_ADDREQUEST;
    request->Time.Seconds = 0;
    request->Time.Microseconds = micros;

    IExec->BeginIO((struct IORequest *)request);
}

void TimerQuit(TimerContext* ctx)
{
    if (!ctx) {
        IExec->DebugPrintF("%s: timer context nullptr\n", __func__);
        return;
    }

    MyDropInterface();

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

BOOL TimerInit(TimerContext* ctx, struct Interrupt* interrupt)
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

    MyGetInterface(ctx->request->Request.io_Device);
    GetFrequency();

    return TRUE;

clean:
    TimerQuit(ctx);

    return FALSE;
}

void TimerWait(const ULONG micros)
{
    TimerContext pauseTimer;

    if (!TimerInit(&pauseTimer, NULL)) {
        puts("Failed to create timer");
        return;
    }

    TimerStart(pauseTimer.request, micros);

    const uint32 timerSig = 1L << pauseTimer.port->mp_SigBit;

    uint32 wait;
    while ((wait = IExec->Wait(timerSig | SIGBREAKF_CTRL_C))) {
        if (wait & timerSig) {
            //IExec->DebugPrintF("Timer finish\n");
            IExec->GetMsg(pauseTimer.port);
            break;
        }

        //puts("Stop pressing CTRL-C :)");
    }

    TimerQuit(&pauseTimer);
}

double TicksToMicros(const uint64 ticks)
{
    return 1000000.0 * ticks / (double)frequency;
}

double GetUptimeInSeconds(void)
{
    if (ITimer) {
        struct TimeVal tv;
        ITimer->GetUpTime(&tv);
        return tv.Seconds + tv.Microseconds / 1000000.0;
    }

    return 0.0;
}

const char* GetUptimeString(void)
{
    const uint64 seconds = (uint64)GetUptimeInSeconds();
    const uint32 secondsInMinute = 60;
    const uint32 secondsInHour = 60 * secondsInMinute;
    const uint32 secondsInDay = 24 * secondsInHour;

    const uint32 d = seconds / secondsInDay;
    const uint32 m = (seconds - d * secondsInDay) / secondsInMinute;
    const uint32 s = (seconds - m * secondsInMinute);

    static char buf[64];

    if (d > 0) {
        snprintf(buf, sizeof(buf), "Uptime: %lu days, %lu minutes, %lu seconds", d, m, s);
    } else if (m > 0) {
        snprintf(buf, sizeof(buf), "Uptime: %lu minutes, %lu seconds", m, s);
    } else {
        snprintf(buf, sizeof(buf), "Uptime: %lu seconds", s);
    }

    return buf;
}

