// CPU time and memory of this process, for the harness's --perf mode.
//
// On its own because it needs <windows.h>, whose macros collide with JUCE -
// the same reason SecretStore.cpp stands alone. Nothing here is in the plugin.
#if defined (_WIN32)

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <psapi.h>

#pragma comment (lib, "psapi.lib")

// User + kernel CPU seconds this process has used so far.
double processCpuSeconds()
{
    FILETIME created, exited, kernel, user;
    if (! GetProcessTimes (GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0.0;

    auto seconds = [] (const FILETIME& f)
    {
        ULARGE_INTEGER u;
        u.LowPart  = f.dwLowDateTime;
        u.HighPart = f.dwHighDateTime;
        return static_cast<double> (u.QuadPart) / 1.0e7;   // 100 ns units
    };
    return seconds (kernel) + seconds (user);
}

// CPU cycles this process has used - exact, where GetProcessTimes moves in
// 15.6 ms steps (0.39% of a core over four seconds, which is the whole signal).
unsigned long long processCycles()
{
    ULONG64 cycles = 0;
    QueryProcessCycleTime (GetCurrentProcess(), &cycles);
    return static_cast<unsigned long long> (cycles);
}

// Working set, in megabytes.
double processMemoryMB()
{
    PROCESS_MEMORY_COUNTERS pmc {};
    if (! GetProcessMemoryInfo (GetCurrentProcess(), &pmc, sizeof (pmc)))
        return 0.0;
    return static_cast<double> (pmc.WorkingSetSize) / (1024.0 * 1024.0);
}

#else
double processCpuSeconds() { return 0.0; }
unsigned long long processCycles() { return 0; }
double processMemoryMB()   { return 0.0; }
#endif
