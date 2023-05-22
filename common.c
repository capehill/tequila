#include "common.h"

#include <proto/exec.h>

APTR AllocateMemory(const size_t size)
{
    if (!size) {
        IExec->DebugPrintF("%s: 0 size alloc\n", __func__);
        return NULL;
    }

    APTR address = IExec->AllocVecTags(size,
        AVT_ClearWithValue, 0,
        AVT_Type, MEMF_PRIVATE,
        TAG_DONE);

    if (!address) {
        IExec->DebugPrintF("%s: failed to allocate %lu bytes\n", __func__, size);
    }

    return address;
}

void FreeMemory(APTR address)
{
    if (!address) {
        IExec->DebugPrintF("%s: nullptr\n", __func__);
    }

    IExec->FreeVec(address);
}

