#include "entryhook.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <detours.h>

namespace EntryHook
{
    long Attach(void** a_function, void* a_detour)
    {
        if (const auto error = DetourTransactionBegin(); error != NO_ERROR) {
            return error;
        }
        DetourUpdateThread(GetCurrentThread());
        if (const auto error = DetourAttach(a_function, a_detour); error != NO_ERROR) {
            DetourTransactionAbort();
            return error;
        }
        return DetourTransactionCommit();
    }
}
