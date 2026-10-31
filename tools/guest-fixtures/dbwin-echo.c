/* dbwin-echo: proves the guest debug-output (DBWIN) channel delivers
 * OutputDebugStringA text to a real listener in the same session
 * (FD-40 proof B). The listener half creates the monitor objects the
 * documented DBWIN protocol requires -- the DBWIN_BUFFER shared
 * section, the DBWIN_BUFFER_READY / DBWIN_DATA_READY events and the
 * DBWinMutex -- then spawns this same program with --sender; the
 * sender emits one known string via OutputDebugStringA. The listener
 * prints what arrived in the shared buffer; RESULT 0 only when the
 * exact token came through.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char TOKEN[] = "DBWIN-ECHO-TOKEN-7f3a9c";

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--sender") == 0)
    {
        OutputDebugStringA(TOKEN);
        return 0;
    }

    HANDLE mutex = CreateMutexA(NULL, FALSE, "DBWinMutex");
    HANDLE ready = CreateEventA(NULL, FALSE, FALSE, "DBWIN_BUFFER_READY");
    HANDLE data  = CreateEventA(NULL, FALSE, FALSE, "DBWIN_DATA_READY");
    HANDLE map   = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL,
                                      PAGE_READWRITE, 0, 4096, "DBWIN_BUFFER");
    if (!mutex || !ready || !data || !map)
    {
        printf("dbwin-echo: SETUP FAIL gle=%lu\n", (unsigned long)GetLastError());
        fflush(stdout);
        return 1;
    }
    char *buf = (char *)MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 4096);
    if (!buf)
    {
        printf("dbwin-echo: MAP FAIL gle=%lu\n", (unsigned long)GetLastError());
        fflush(stdout);
        return 1;
    }

    /* buffer starts free: signal readiness before the sender runs */
    SetEvent(ready);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "\"%s\" --sender", argv[0]);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        printf("dbwin-echo: SPAWN FAIL gle=%lu\n", (unsigned long)GetLastError());
        fflush(stdout);
        return 1;
    }

    DWORD wr = WaitForSingleObject(data, 60000);
    if (wr != WAIT_OBJECT_0)
    {
        printf("dbwin-echo: RESULT 1 no delivery (wait=%lu)\n", (unsigned long)wr);
        fflush(stdout);
        return 1;
    }
    DWORD pid = 0;
    memcpy(&pid, buf, sizeof(DWORD));
    const char *msg = buf + sizeof(DWORD);
    printf("dbwin-echo: RECEIVED pid=%lu msg=\"%s\"\n", (unsigned long)pid, msg);
    int ok = strstr(msg, TOKEN) != NULL;
    printf("dbwin-echo: RESULT %d\n", ok ? 0 : 1);
    fflush(stdout);
    /* release the sender: buffer is free again */
    SetEvent(ready);
    WaitForSingleObject(pi.hProcess, 30000);
    return ok ? 0 : 1;
}
