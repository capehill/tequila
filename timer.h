#ifndef TIMER_H
#define TIMER_H

#include <proto/timer.h>

typedef struct TimerContext {
    struct MsgPort* port;
    struct TimeRequest* request;
    BYTE device;
} TimerContext;

typedef struct MyClock {
    union {
        uint64 ticks;
        struct EClockVal clockVal;
    } un;
} MyClock;

void TimerStart(struct TimeRequest* request, ULONG micros);
void TimerQuit(TimerContext* ctx);
BOOL TimerInit(TimerContext* ctx, struct Interrupt* interrupt);
void TimerWait(ULONG micros);
double TicksToMicros(uint64 ticks);

#endif // TIMER_H
