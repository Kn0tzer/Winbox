/* Wine clock-page consistency and timed waits, without graphics or input. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int main(void) {
    OutputDebugStringA("win32-clock: BEGIN\n");
    LARGE_INTEGER frequency, first, now;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 || !QueryPerformanceCounter(&first)) return 1;
    ULONGLONG start = GetTickCount64(), previous = start;
    DWORD start32 = GetTickCount();
    // Sleep permits shorter waits below the system clock resolution. Use
    // intervals comfortably above Wine's clock-page update quantum.
    for (int i = 0; i < 4; ++i) {
        Sleep(100);
        ULONGLONG tick = GetTickCount64();
        if (tick < previous || !QueryPerformanceCounter(&now) || now.QuadPart < first.QuadPart) return 2;
        previous = tick;
    }
    ULONGLONG elapsed = previous - start;
    DWORD elapsed32 = GetTickCount() - start32;
    LONGLONG perf_ms = (now.QuadPart - first.QuadPart) * 1000 / frequency.QuadPart;
    char text[256];
    snprintf(text, sizeof(text), "win32-clock: ELAPSED tick64=%llu tick32=%lu qpc=%lld\n",
        (unsigned long long)elapsed, elapsed32, (long long)perf_ms);
    OutputDebugStringA(text);
    if (elapsed < 200 || elapsed32 < 200 || perf_ms < 200) {
        OutputDebugStringA("win32-clock: FAIL clocks did not advance\n"); return 3;
    }
    OutputDebugStringA("win32-clock: RESULT 0\n");
    return 0;
}
