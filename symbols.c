#include "symbols.h"
#include "common.h"

#include <proto/exec.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_SYMBOLS 200

struct DebugIFace* IDebug;

typedef struct SymbolInfo {
    size_t count;
    ULONG* address;
    char moduleName[NAME_LEN];
    char functionName[NAME_LEN];
} SymbolInfo;

static void Symbol(const ULONG* address, SymbolInfo* symbolInfo)
{
    struct DebugSymbol* ds = IDebug->ObtainDebugSymbol(address, NULL);

    if (ds) {
        snprintf(symbolInfo->moduleName, NAME_LEN, "%s", ds->Name);
        snprintf(symbolInfo->functionName, NAME_LEN, "%s", ds->SourceFunctionName);
        IDebug->ReleaseDebugSymbol(ds);
    } else {
        snprintf(symbolInfo->moduleName, NAME_LEN, "Not available");
        //symbolInfo->functionName[0] = '\0';
        snprintf(symbolInfo->functionName, NAME_LEN, "%p", address);
        //IExec->DebugPrintF("%p\n", address);
    }
}

static size_t validSymbols = 0;

static void MaybeLookup(ULONG* address, SymbolInfo* symbolInfo)
{
    static ULONG* lastAddress = NULL;

    // Try to avoid unnecessary symbol lookups
    if (address != lastAddress) {
        Symbol(address, symbolInfo);
        lastAddress = address;
    }
}

static BOOL FindSymbol(const ULONG* address, SymbolInfo* symbols, SymbolInfo* symbolInfoBuffer, const size_t unique)
{
    for (size_t u = 0; u < unique; u++) {
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

static size_t AddSymbol(ULONG* address, SymbolInfo* symbols, SymbolInfo* symbolInfoBuffer, size_t unique)
{
    strcpy(symbols[unique].moduleName, symbolInfoBuffer->moduleName); // TODO: take buffer size into account
    strcpy(symbols[unique].functionName, symbolInfoBuffer->functionName); // TODO: take buffer size into account
    symbols[unique].count = 1;
    symbols[unique].address = address;
    return ++unique;
}

static size_t PrepareSymbols(SymbolInfo* symbols)
{
    size_t unique = 0;

    IDebug = (struct DebugIFace *)IExec->GetInterface((struct Library *)SysBase, "debug", 1, NULL);

    if (!IDebug) {
        puts("Failed to get IDebug");
        return 0;
    }

    SymbolInfo* symbolInfo = AllocateMemory(sizeof(SymbolInfo));
    if (!symbolInfo) {
        puts("Failed to allocate symbol info buffer");
        return 0;
    }

    printf("\nProcessing symbol data (max addresses %lu)...\n", ctx.maxAddresses);

    const size_t part = ctx.maxAddresses / 10;
    size_t nextMark = part;

    for (size_t i = 0; i < ctx.maxAddresses; i++) {
        ULONG* const currAddress = ctx.addresses[i];

        if (!currAddress) {
            continue;
        }

        validSymbols++;

        if (i >= nextMark) {
            printf("%u/%lu\n", i, ctx.maxAddresses);
            nextMark += part;
        }

        MaybeLookup(currAddress, symbolInfo);

        const BOOL found = FindSymbol(currAddress, symbols, symbolInfo, unique);

        if (!found && unique < MAX_SYMBOLS) {
            unique = AddSymbol(currAddress, symbols, symbolInfo, unique);
        }
    }

    FreeMemory(symbolInfo);

    IExec->DropInterface((struct Interface *)IDebug);
    IDebug = NULL;

    printf("Found %u unique and %u non-zero symbols\n", unique, validSymbols);

    return unique;
}

static int CompareCounts(const void* first, const void* second)
{
    const SymbolInfo* a = first;
    const SymbolInfo* b = second;

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

static void ShowByModule(struct SymbolInfo* symbols, const size_t unique)
{
    char** moduleNames = AllocateMemory(unique * sizeof(char *));

    if (!moduleNames) {
        puts("Failed to allocate module name buffer");
        return;
    }

    const size_t uniqueModules = PrepareModules(symbols, unique, moduleNames);

    printf("\nSorted by module:\n");

    for (size_t m = 0; m < uniqueModules; m++) {
        printf("\n%10s %10s %64s '%s'\n", "Sample %", "Count", "Function in module", moduleNames[m]);

        for (size_t i = 0; i < unique; i++) {
            if (strcmp(moduleNames[m], symbols[i].moduleName) == 0) {
                const float percentage = 100.0f * symbols[i].count / validSymbols;
                printf("%10.2f %10u %64s\n", percentage, symbols[i].count, symbols[i].functionName);
            }
        }

        free(moduleNames[m]);
    }

    FreeMemory(moduleNames);
}

void ShowSymbols()
{
    SymbolInfo* symbols = AllocateMemory(sizeof(SymbolInfo) * MAX_SYMBOLS);

    if (!symbols) {
        puts("Failed to allocate symbol buffer");
        return;
    }

    const size_t unique = PrepareSymbols(symbols);

    puts("Sorting symbols...");

    qsort(symbols, unique, sizeof(SymbolInfo), CompareCounts);

    printf("\n%10s %10s %64s\n", "Sample %", "Count", "Symbol name (module + function)");

    for (size_t i = 0; i < unique; i++) {
        const float percentage = 100.0f * symbols[i].count / validSymbols;

        char name[NAME_LEN];
        snprintf(name, NAME_LEN, "%s %s", symbols[i].moduleName, symbols[i].functionName);

        printf("%10.2f %10u %64s\n", percentage, symbols[i].count, name);
    }

    ShowByModule(symbols, unique);

    FreeMemory(symbols);
}

