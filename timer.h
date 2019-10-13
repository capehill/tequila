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

void timerStart(struct TimeRequest* request, ULONG micros);
void timerQuit(TimerContext* ctx);
BOOL timerInit(TimerContext* ctx, struct Interrupt* interrupt);
void timerWait(ULONG micros);
double ticksToMicros(uint64 ticks);

#endif // TIMER_H
