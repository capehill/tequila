#include "common.h"

#include <proto/exec.h>

APTR allocMem(size_t size)
{
    if (!size) {
        IExec->DebugPrintF("%s: 0 size alloc\n", __func__);
        return NULL;
    }

    APTR address = IExec->AllocVecTags(size,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!address) {
        IExec->DebugPrintF("%s: failed to allocate %lu bytes\n", size);
    }

    return address;
}

void freeMem(APTR address)
{
    if (!address) {
        IExec->DebugPrintF("%s: nullptr\n", __func__);
    }

    IExec->FreeVec(address);
}


size_t stringLen(const char* str)
{
    if (!str) {
        IExec->DebugPrintF("%s - nullptr\n", __func__);
        return 0;
    }

    size_t len = 0;

    while (*str++) {
        ++len;
    }

    return len;
}

void copyString(char* const to, const char* const from, size_t len)
{
    if (to && from && len > 0) {
        const size_t need = stringLen(to) + len;

        if (need >= NAME_LEN) {
            IExec->DebugPrintF("String buffer too short? Needs %lu+ bytes\n", need);
        }

        for (size_t i = 0; i < len; i++) {
            to[i] = from[i];
        }
        to[len] = '\0';
    }
}
