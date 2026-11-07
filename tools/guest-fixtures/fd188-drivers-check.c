/* FD-188 guest check: are the kernel-driver files present where the loader
 * looks for them? Reports, via OutputDebugString, the attributes of each
 * driver file under C:\windows\system32\drivers\, listings of the drivers
 * and system32 directories, and the update-timestamp state, plus two
 * control files that are known to load. Also reports the service state of
 * each driver (QueryServiceStatus): the WineBus service must be RUNNING
 * for FD-188 to be closed -- the driver-load traces for the service-started
 * WineBus driver are not visible on the session's ntoskrnl channel, so the
 * service state is the ground truth for it. */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static void emit(const char *format, ...) {
    char message[512];
    va_list ap;
    va_start(ap, format);
    vsnprintf(message, sizeof(message), format, ap);
    va_end(ap);
    OutputDebugStringA(message);
}

static void list_dir(const char *pattern, const char *label) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        emit("fd188-check: %s listing FAILED err=%lu\n", label,
             (unsigned long)GetLastError());
        return;
    }
    {
        unsigned n = 0;
        do {
            if (n < 12) emit("fd188-check: %s entry: %s\n", label, fd.cFileName);
            n++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        emit("fd188-check: %s entries=%u\n", label, n);
    }
}

static void service_state(const char *name) {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        emit("fd188-check: service %s scm err=%lu\n", name,
             (unsigned long)GetLastError());
        return;
    }
    SC_HANDLE svc = OpenServiceA(scm, name, SERVICE_QUERY_STATUS);
    if (!svc) {
        emit("fd188-check: service %s open err=%lu\n", name,
             (unsigned long)GetLastError());
        CloseServiceHandle(scm);
        return;
    }
    SERVICE_STATUS st;
    if (!QueryServiceStatus(svc, &st)) {
        emit("fd188-check: service %s query err=%lu\n", name,
             (unsigned long)GetLastError());
    } else {
        emit("fd188-check: service %s state=%lu\n", name,
             (unsigned long)st.dwCurrentState);
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

int main(void) {
    static const char *files[] = {
        "C:\\windows\\system32\\drivers\\mountmgr.sys",
        "C:\\windows\\system32\\drivers\\winebus.sys",
        "C:\\windows\\system32\\drivers\\nsiproxy.sys",
        "C:\\windows\\system32\\drivers\\ndis.sys",
        "C:\\windows\\system32\\drivers\\wineusb.sys",
        "C:\\windows\\system32\\kernel32.dll",
        "C:\\windows\\system32\\ntoskrnl.exe",
        "C:\\windows\\system32\\winedevice.exe",
        "C:\\windows\\system32\\drivers",
    };
    unsigned i;
    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        DWORD attrs = GetFileAttributesA(files[i]);
        emit("fd188-check: %s attrs=0x%08lx\n", files[i], (unsigned long)attrs);
    }
    list_dir("C:\\windows\\system32\\drivers\\*", "drivers");
    list_dir("C:\\windows\\system32\\*", "system32");
    {
        HANDLE h = CreateFileA("Z:\\home\\username\\.wine\\.update-timestamp",
                               GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            emit("fd188-check: update-timestamp unreadable err=%lu\n",
                 (unsigned long)GetLastError());
        } else {
            char buf[64];
            DWORD read = 0;
            ReadFile(h, buf, sizeof(buf) - 1, &read, NULL);
            buf[read] = 0;
            CloseHandle(h);
            emit("fd188-check: update-timestamp=[%s]\n", buf);
        }
    }
    service_state("WineBus");
    service_state("MountMgr");
    service_state("nsiproxy");
    service_state("NDIS");
    service_state("wineusb");
    emit("fd188-check: DONE\n");
    return 0;
}
