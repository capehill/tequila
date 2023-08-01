#include "symbols.h"
#include "common.h"

#include <proto/exec.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_SYMBOLS 200
#define MAX_STACK_TRACES 200
#define LOWEST_VALID_CODE_ADDRESS 0x100000 /* Just a random number from magic hat */

struct DebugIFace* IDebug;

typedef struct SymbolInfo {
    size_t count;
    ULONG* address;
    char moduleName[NAME_LEN];
    char functionName[NAME_LEN];
} SymbolInfo;

typedef struct StackTrace {
    uint32 id;
    // struct Task* task; // TODO: add task info?
    size_t count;
    uint32* ip[MAX_STACK_DEPTH];
} StackTrace;

// TODO: C++ name demangling needed

static void Symbol(const ULONG* address, SymbolInfo* symbolInfo)
{
    // Note: there is a bug in kernel < 54.47 (???) that requires address increment of 4 bytes
    const int offset = ctx.symbolLookupWorkaroundNeeded ? 1 : 0;

    struct DebugSymbol* ds = IDebug->ObtainDebugSymbol(address + offset, NULL);

    if (ds) {
        snprintf(symbolInfo->moduleName, NAME_LEN, "%s", ds->Name);
        snprintf(symbolInfo->functionName, NAME_LEN, "%s", ds->SourceFunctionName);
        IDebug->ReleaseDebugSymbol(ds);
    } else {
        const char* detail = "";
        if ((uint32)address < LOWEST_VALID_CODE_ADDRESS) {
            detail = " (invalid address?)";
        } else if ((uint32)address & 0x3) {
            detail = " (invalid alignment?)";
        }

        snprintf(symbolInfo->moduleName, NAME_LEN, "Symbol not available%s", detail);
        symbolInfo->functionName[0] = '\0';
        //snprintf(symbolInfo->functionName, NAME_LEN, "%p", address);
        //IExec->DebugPrintF("%p\n", address);
    }
}

static void MaybeLookup(ULONG* address, SymbolInfo* symbolInfo)
{
    static ULONG* lastAddress = NULL;

    // Try to avoid unnecessary symbol lookups
    if (address != lastAddress) {
        Symbol(address, symbolInfo);
        lastAddress = address;
    }
}

static BOOL FindSymbol(const ULONG* address, SymbolInfo* symbols, SymbolInfo* symbolInfoBuffer)
{
    for (size_t u = 0; u < ctx.profiling.uniqueSymbols; u++) {
        if (address == symbols[u].address ||
            ((strcmp(symbolInfoBuffer->moduleName, symbols[u].moduleName) == 0) &&
             (strcmp(symbolInfoBuffer->functionName, symbols[u].functionName) == 0)))
        {
            symbols[u].count++;
            return TRUE;
        }
    }

    return FALSE;
}

static void AddSymbol(ULONG* address, SymbolInfo* symbols, SymbolInfo* symbolInfoBuffer)
{
    SymbolInfo* symbol = &symbols[ctx.profiling.uniqueSymbols];

    strcpy(symbol->moduleName, symbolInfoBuffer->moduleName); // TODO: take buffer size into account
    strcpy(symbol->functionName, symbolInfoBuffer->functionName); // TODO: take buffer size into account
    symbol->count = 1;
    symbol->address = address;

    ++ctx.profiling.uniqueSymbols;
}

static void AddUniqueSymbol(uint32* address, SymbolInfo* symbols)
{
    SymbolInfo si;
    ctx.profiling.validSymbols++;

    MaybeLookup(address, &si);

    if (!FindSymbol(address, symbols, &si)) {
        if (ctx.profiling.uniqueSymbols < MAX_SYMBOLS) {
            AddSymbol(address, symbols, &si);
        } else {
            puts("Too many unique symbols");
        }
    }
}

static BOOL FindStackTrace(uint32* ip[MAX_STACK_DEPTH], StackTrace* traces)
{
    uint32 id = 0;
    for (size_t frame = 0; frame < MAX_STACK_DEPTH; frame++) {
        if (ip[frame]) {
            id += (uint32)ip[frame];
        } else {
            break;
        }
    }

    for (size_t i = 0; i < ctx.profiling.uniqueStackTraces; i++) {
        if (traces[i].id == id) {
            traces[i].count++;
            //printf("%s - id %lu, count %u\n", __func__, id, traces[i].count);
            return TRUE;
        }
    }

    return FALSE;
}

static void AddStackTrace(uint32* ip[MAX_STACK_DEPTH], StackTrace* traces, SymbolInfo* symbols)
{
    StackTrace* t = &traces[ctx.profiling.uniqueStackTraces];
    t->count = 1;
    t->id = 0; // TODO: is hash needed?

    for (size_t frame = 0; frame < MAX_STACK_DEPTH; frame++) {
        t->ip[frame] = ip[frame];
        if (t->ip[frame]) {
            t->id += (uint32)t->ip[frame];
            if (frame == 0) {
                AddUniqueSymbol(t->ip[frame], symbols);
            }
        } else {
            break;
        }
    }

    //printf("%s - id %lu\n", __func__, t->id);

   ++ctx.profiling.uniqueStackTraces;
}

static void AddEmptyStackTrace(SymbolInfo* symbols, StackTrace* traces)
{
    // Initialize dummy empty stack trace.
    // If profiling buffer is only filled partially, it should be there.
    uint32* dummy = NULL;
    AddStackTrace(&dummy, traces, symbols);
}

static void PrepareSymbols(SymbolInfo* symbols, StackTrace* traces)
{
    printf("\nPlease wait and do not quit profiled programs...\n");
    printf("\nProcessing symbol data (stack traces %lu)...\n", ctx.profiling.stackTraces);

    const size_t part = ctx.profiling.stackTraces / 10;
    size_t nextMark = part;
    MyClock start, finish;

    if (ctx.debugMode) {
        ITimer->ReadEClock(&start.un.clockVal);
    }

    AddEmptyStackTrace(symbols, traces);

    for (size_t trace = 0; trace < ctx.profiling.stackTraces; trace++) {
        const size_t offset = trace * MAX_STACK_DEPTH;

        if (!FindStackTrace(&ctx.profiling.addresses[offset], traces)) {
            if (ctx.profiling.uniqueStackTraces < MAX_STACK_TRACES) {
                AddStackTrace(&ctx.profiling.addresses[offset], traces, symbols);
            } else {
                puts("Too many unique stack traces");
            }
        }

        if (trace >= nextMark) {
            printf("%u/%lu\n", trace, ctx.profiling.stackTraces);
            nextMark += part;
        }
    }

    if (ctx.debugMode) {
        ITimer->ReadEClock(&finish.un.clockVal);
        const uint64 duration = finish.un.ticks - start.un.ticks;
        printf("\nPreparing symbols took %g ms\n", TicksToMicros(duration) / 1000.0);
    }

    printf("\nFound %u unique and %u non-zero symbols\n", ctx.profiling.uniqueSymbols, ctx.profiling.validSymbols);
    printf("Found %u unique stack traces\n", ctx.profiling.uniqueStackTraces);
}

static int CompareCounts(const void* first, const void* second)
{
    const SymbolInfo* a = first;
    const SymbolInfo* b = second;

    if (a->count > b->count) return -1;
    if (a->count < b->count) return 1;

    return 0;
}

static int CompareStackTraces(const void* first, const void* second)
{
    const StackTrace* a = first;
    const StackTrace* b = second;

    if (a->count > b->count) return -1;
    if (a->count < b->count) return 1;

    return 0;
}

static size_t PrepareModules(SymbolInfo* symbols, const size_t unique, char** moduleNames)
{
    size_t uniqueModules = 0;

    for (size_t i = 0; i < unique; i++) {
        BOOL found = FALSE;

        for (size_t m = 0; m < uniqueModules; m++) {
            if (strcmp(moduleNames[m], symbols[i].moduleName) == 0) {
                found = TRUE;
                break;
            }
        }

        if (!found) {
            moduleNames[uniqueModules] = strdup(symbols[i].moduleName);
            if (moduleNames[uniqueModules]) {
                uniqueModules++;
            } else {
                puts("Failed to duplicate module name");
            }
        }
    }

    return uniqueModules;
}

static void ShowByModule(SymbolInfo* symbols)
{
    char** moduleNames = AllocateMemory(ctx.profiling.uniqueSymbols * sizeof(char *));

    if (!moduleNames) {
        puts("Failed to allocate module name buffer");
        return;
    }

    const size_t uniqueModules = PrepareModules(symbols, ctx.profiling.uniqueSymbols, moduleNames);

    printf("\nSorted by module:\n");

    for (size_t m = 0; m < uniqueModules; m++) {
        printf("\n%10s %10s %64s '%s'\n", "Sample %", "Count", "Function in module", moduleNames[m]);

        for (size_t i = 0; i < ctx.profiling.uniqueSymbols; i++) {
            if (strcmp(moduleNames[m], symbols[i].moduleName) == 0) {
                const float percentage = 100.0f * symbols[i].count / ctx.profiling.validSymbols;
                printf("%10.2f %10u %64s\n", percentage, symbols[i].count, symbols[i].functionName);
            }
        }

        free(moduleNames[m]);
    }

    FreeMemory(moduleNames);
}

// TODO: how to deal with similar but not identical stack traces?
static void ShowByStackTraces(StackTrace* traces)
{
    printf("\nSorting stack traces...\n");

    qsort(traces, ctx.profiling.uniqueStackTraces, sizeof(StackTrace), CompareStackTraces);

    printf("\nUnique stack traces:\n");

    SymbolInfo si;

    for (size_t i = 0; i < ctx.profiling.uniqueStackTraces; i++) {
        printf("\nStack trace %u (count %u - %.2f%%):\n", i, traces[i].count, 100.0f * traces[i].count / ctx.profiling.stackTraces);
        if (traces[i].id == 0) {
            // TODO: keep book on actual collected stackTraces in case profiling is stopped before buffer is complete?
            printf("  Empty stack trace\n");
        }

        for (size_t frame = 0; frame < MAX_STACK_DEPTH; frame++) {
            const uint32* const ip = traces[i].ip[frame];
            if (ip) {
                Symbol(ip, &si);
                printf("  Frame %u, ip %p - %s @ %s\n", frame, traces[i].ip[frame], si.functionName, si.moduleName);
            } else {
                break;
            }
        }
    }
}

static void ShowStatistics(void)
{
    printf("\nStatistics:\n");
    printf("  %u stack frame loop(s) detected\n", ctx.profiling.stackFrameLoopDetected);
    printf("  %u stack frame alignment issue(s) detected\n", ctx.profiling.stackFrameNotAligned);
    printf("  %u stack frame out-of-bound issue(s) detected\n", ctx.profiling.stackFrameOutOfBounds);
}

void ShowSymbols(void)
{
    IDebug = (struct DebugIFace *)IExec->GetInterface((struct Library *)SysBase, "debug", 1, NULL);

    SymbolInfo* symbols = AllocateMemory(sizeof(SymbolInfo) * MAX_SYMBOLS);
    StackTrace* traces = AllocateMemory(sizeof(StackTrace) * MAX_STACK_TRACES);

    if (!IDebug) {
        puts("Failed to get IDebug");
        goto out;
    }

    if (!symbols) {
        puts("Failed to allocate symbol buffer");
        goto out;
    }

    if (!traces) {
        puts("Failed to allocate stack trace buffer");
        goto out;
    }

    PrepareSymbols(symbols, traces);

    puts("Sorting symbols...");

    qsort(symbols, ctx.profiling.uniqueSymbols, sizeof(SymbolInfo), CompareCounts);

    printf("\n%10s %10s %64s\n", "Sample %", "Count", "Symbol name (module + function)");

    for (size_t i = 0; i < ctx.profiling.uniqueSymbols; i++) {
        const float percentage = 100.0f * symbols[i].count / ctx.profiling.validSymbols;

        char name[NAME_LEN];
        snprintf(name, NAME_LEN, "%s %s", symbols[i].moduleName, symbols[i].functionName);

        printf("%10.2f %10u %64s\n", percentage, symbols[i].count, name);
    }

    ShowByModule(symbols);
    ShowByStackTraces(traces);
    ShowStatistics();

out:
    FreeMemory(traces);
    FreeMemory(symbols);

    IExec->DropInterface((struct Interface *)IDebug);
    IDebug = NULL;
}

