/* FD-188 diagnostic: what HID devices does the guest's PlugPlay layer
 * actually have? Enumerates the HID device interface class via SetupDi
 * and reports each device's instance ID, plus the WineBus service state
 * and the C:\windows\inf\ listing (so a missing function-driver install
 * shows up as a device with no driver rather than as no device at all).
 * Reports via OutputDebugString. */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <setupapi.h>

/* HID device interface class GUID (mingw's hidsdi.h does not declare it):
 * {4D1E55B2-F16F-11CF-88CB-001111000030}. */
static const GUID GUID_DEVINTERFACE_HID =
    {0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};

static void emit(const char *format, ...) {
    char message[512];
    va_list ap;
    va_start(ap, format);
    vsnprintf(message, sizeof(message), format, ap);
    va_end(ap);
    OutputDebugStringA(message);
}

int main(void) {
    HDEVINFO set = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_HID, NULL, NULL,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        emit("fd188-hid: SetupDiGetClassDevs FAILED err=%lu\n",
             (unsigned long)GetLastError());
    } else {
        SP_DEVINFO_DATA dev = {sizeof(dev)};
        DWORD i = 0;
        unsigned n = 0;
        while (SetupDiEnumDeviceInfo(set, i, &dev)) {
            char id[256] = {0};
            if (SetupDiGetDeviceInstanceIdA(set, &dev, id, sizeof(id), NULL))
                emit("fd188-hid: device %u id=%s\n", n, id);
            else
                emit("fd188-hid: device %u id=? err=%lu\n", n,
                     (unsigned long)GetLastError());
            n++;
            i++;
        }
        emit("fd188-hid: HID devices=%u\n", n);
        SetupDiDestroyDeviceInfoList(set);
    }
    {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA("C:\\windows\\inf\\*", &fd);
        if (h == INVALID_HANDLE_VALUE) {
            emit("fd188-hid: inf listing FAILED err=%lu\n",
                 (unsigned long)GetLastError());
        } else {
            unsigned n = 0;
            do {
                if (n < 12) emit("fd188-hid: inf entry: %s\n", fd.cFileName);
                n++;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
            emit("fd188-hid: inf entries=%u\n", n);
        }
    }
    {
        SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
        SC_HANDLE svc = scm ? OpenServiceA(scm, "WineBus", SERVICE_QUERY_STATUS) : NULL;
        if (!svc) {
            emit("fd188-hid: WineBus service open err=%lu\n",
                 (unsigned long)GetLastError());
        } else {
            SERVICE_STATUS st;
            if (QueryServiceStatus(svc, &st))
                emit("fd188-hid: service WineBus state=%lu\n", (unsigned long)st.dwCurrentState);
            CloseServiceHandle(svc);
        }
        if (scm) CloseServiceHandle(scm);
    }
    emit("fd188-hid: DONE\n");
    return 0;
}
