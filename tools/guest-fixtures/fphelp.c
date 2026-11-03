/* Helper DLL for fpprobe8: a cross-module callee that is NOT msvcrt. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

__declspec(dllexport) double fphelp_id(double v) { return v * 1.0; }
__declspec(dllexport) unsigned fphelp_tick(void) { return 0; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)h; (void)reason; (void)r;
    return TRUE;
}
