#include "timer.h"

#include <stdio.h>

#include <proto/exec.h>
#include <dos/dos.h>

void timerStart(struct TimeRequest* request, ULONG micros)
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

void timerQuit(TimerContext* ctx)
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

BOOL timerInit(TimerContext* ctx, struct Interrupt* interrupt)
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

void timerWait(ULONG micros)
{
    TimerContext pauseTimer;

    if (!timerInit(&pauseTimer, NULL)) {
        puts("Failed to create timer");
        return;
    }

    timerStart(pauseTimer.request, micros);

    const uint32 timerSig = 1L << pauseTimer.port->mp_SigBit;

    uint32 wait;
    while ((wait = IExec->Wait(timerSig | SIGBREAKF_CTRL_C))) {
        if (wait & timerSig) {
            IExec->DebugPrintF("Timer finish\n");
            IExec->GetMsg(pauseTimer.port);
            break;
        }

        //puts("Stop pressing CTRL-C :)");
    }

    timerQuit(&pauseTimer);
}

