#pragma once

// Hooks a function's entry with Microsoft Detours. Kept in its own library so that <windows.h> never meets
// CommonLib's declarations of the same functions.
namespace EntryHook
{
    // a_function holds the address to hook; on success it's replaced by a pointer that calls the original.
    // Returns 0 on success, otherwise the Detours error code.
    long Attach(void** a_function, void* a_detour);
}
