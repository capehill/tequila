#include "symbols.h"
#include "common.h"

#include <proto/exec.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_SYMBOLS 100

struct DebugIFace* IDebug;

typedef struct SymbolInfo {
    size_t count;
    ULONG* address;
    char nameBuffer[NAME_LEN];
} SymbolInfo;

ULONG** addresses = NULL;
ULONG maxAddresses = 0;

static void symbol(const ULONG* address, char* nameBuffer)
{
    struct DebugSymbol* ds = IDebug->ObtainDebugSymbol(address, NULL);

    nameBuffer[0] = '\0';

    if (ds) {
        //IExec->DebugPrintF("%s %s\n", ds->Name, ds->SourceFunctionName);
        //snprintf(nameBuffer, NAME_LEN, "%s %s %s %s", ds->Name, ds->SourceFileName, ds->SourceFunctionName, ds->SourceBaseName);
        snprintf(nameBuffer, NAME_LEN, "%s %s", ds->Name, ds->SourceFunctionName);
        IDebug->ReleaseDebugSymbol(ds);
    }
}

static size_t validSymbols = 0;

static void maybeLookup(ULONG* address, char* symbolNameBuffer)
{
    static ULONG* lastAddress = NULL;

    // Try to avoid unnecessary symbol lookups
    if (address != lastAddress) {
        symbol(address, symbolNameBuffer);
        lastAddress = address;
    }
}

static BOOL findSymbol(ULONG* address, SymbolInfo* symbols, char* symbolNameBuffer, size_t unique)
{
    for (size_t u = 0; u < unique; u++) {
        if (address == symbols[u].address || strcmp(symbolNameBuffer, symbols[u].nameBuffer) == 0) {
            symbols[u].count++;
            return TRUE;
        }
    }

    return FALSE;
}

static size_t addSymbol(ULONG* address, SymbolInfo* symbols, char* symbolNameBuffer, size_t unique)
{
    strcpy(symbols[unique].nameBuffer, symbolNameBuffer); // TODO: take buffer size into account
    symbols[unique].count = 1;
    symbols[unique].address = address;
    return ++unique;
}

static size_t prepareSymbols(SymbolInfo* symbols)
{
    size_t unique = 0;

    IDebug = (struct DebugIFace *)IExec->GetInterface((struct Library *)SysBase, "debug", 1, NULL);

    if (!IDebug) {
        puts("Failed to get IDebug");
        return 0;
    }

    char* symbolNameBuffer = allocMem(NAME_LEN);
    if (!symbolNameBuffer) {
        puts("Failed to allocate symbol name buffer");
        return 0;
    }

    printf("\nProcessing symbol data (max addresses %lu)...\n", maxAddresses);

    const size_t part = maxAddresses / 10;
    size_t nextMark = part;

    for (size_t i = 0; i < maxAddresses; i++) {
        ULONG* const currAddress = addresses[i];

        if (!currAddress) {
            continue;
        }

        validSymbols++;

        if (i >= nextMark) {
            printf("%u/%lu\n", i, maxAddresses);
            nextMark += part;
        }

        maybeLookup(currAddress, symbolNameBuffer);

        const BOOL found = findSymbol(currAddress, symbols, symbolNameBuffer, unique);

        if (!found && unique < (MAX_SYMBOLS - 1)) {
            unique = addSymbol(currAddress, symbols, symbolNameBuffer, unique);
        }
    }

    freeMem(symbolNameBuffer);

    if (IDebug) {
        IExec->DropInterface((struct Interface *)IDebug);
        IDebug = NULL;
    }

    printf("Found %u unique and %u valid symbols\n", unique, validSymbols);

    return unique;
}

static int comparison(const void* first, const void* second)
{
    const SymbolInfo* a = first;
    const SymbolInfo* b = second;

    if (a->count > b->count) return -1;
    if (a->count < b->count) return 1;

    return 0;
}

void showSymbols()
{
    SymbolInfo* symbols = allocMem(sizeof(SymbolInfo) * MAX_SYMBOLS);

    if (!symbols) {
        puts("Failed to allocate symbol buffer");
        return;
    }

    const size_t unique = prepareSymbols(symbols);

    puts("Sorting symbols...");

    qsort(symbols, unique, sizeof(SymbolInfo), comparison);

    printf("\n%10s %10s %64s\n", "Sample %", "Count", "Symbol name (module + function)");

    for (size_t i = 0; i < unique; i++) {
        const float percentage = 100.0f * symbols[i].count / validSymbols;
        printf("%10.2f %10u %64s\n", percentage, symbols[i].count, symbols[i].nameBuffer);
    }

    freeMem(symbols);
}

