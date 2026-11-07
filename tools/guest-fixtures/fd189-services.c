/* fd189-services.c -- FD-189 guest proof fixture (both architectures).
 *
 * FD-189: the shipped 64-bit guest prefix has no PlugPlay or bus-driver
 * registration. The gamepad lane's diagnostic probe found, on the 64-bit
 * side, OpenService answering error 1060 for PlugPlay, WineBus and
 * WineUsb, and an empty root device enumeration -- while the 32-bit
 * prefix (produced by a real wineboot) carries all three registrations
 * and the ROOT\WINE bus devices, and its probe shows PlugPlay running
 * and both bus services registered.
 *
 * This fixture is that probe, in fixture form: it asks the Service
 * Control Manager for the three services and enumerates the devices the
 * ROOT enumerator reports, printing one line per check and a final
 * RESULT line. RESULT 0 means all three services are present and at
 * least the two ROOT\WINE bus devices (WINEBUS, WINEUSB) enumerate.
 * Presence is the criterion -- whether a service is running is reported
 * but not required (driver start is FD-173's separate question).
 *
 * Against the pre-change 64-bit prefix the fixture fails with the filed
 * signature (all three services err=1060, zero ROOT\WINE devices); on
 * the 32-bit side it passes unchanged, which is the control proving the
 * probe itself works.
 */
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN64
#define TAG "fd189-64"
#else
#define TAG "fd189-32"
#endif

static int failures;
static int checks;

static void report(const char *name, int ok, const char *detail)
{
    checks++;
    if (!ok) failures++;
    printf(TAG ": %-52s %s %s\n", name, ok ? "ok  " : "FAIL", detail);
    fflush(stdout);
}

static void group(const char *name)
{
    printf(TAG ": group %s\n", name);
    fflush(stdout);
}

/* ---- service registrations -------------------------------------------- */
static void check_service(SC_HANDLE scm, const char *name)
{
    char detail[128];
    SC_HANDLE svc = OpenServiceA(scm, name, SERVICE_QUERY_STATUS);
    if (!svc) {
        snprintf(detail, sizeof(detail), "OpenService err=%lu", GetLastError());
        report(name, 0, detail);
        return;
    }
    SERVICE_STATUS status;
    if (QueryServiceStatus(svc, &status))
        snprintf(detail, sizeof(detail), "present state=%lu", status.dwCurrentState);
    else
        snprintf(detail, sizeof(detail), "present status-err=%lu", GetLastError());
    report(name, 1, detail);
    CloseServiceHandle(svc);
}

/* ---- root enumeration --------------------------------------------------- */
static void check_root_enumeration(void)
{
    char detail[256];
    HDEVINFO set = SetupDiGetClassDevsA(NULL, "ROOT", NULL,
                                         DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        snprintf(detail, sizeof(detail), "SetupDiGetClassDevs err=%lu", GetLastError());
        report("root-enum-open", 0, detail);
        report("root-wine-devices", 0, "set not opened");
        return;
    }
    report("root-enum-open", 1, "handle ok");
    SP_DEVINFO_DATA dev;
    dev.cbSize = sizeof(dev);
    int total = 0, wine = 0;
    char id[MAX_DEVICE_ID_LEN];
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); i++) {
        total++;
        if (SetupDiGetDeviceInstanceIdA(set, &dev, id, sizeof(id), NULL)) {
            printf(TAG ":   device %s\n", id);
            fflush(stdout);
            if (_strnicmp(id, "ROOT\\WINE", 9) == 0)
                wine++;
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    snprintf(detail, sizeof(detail), "total=%d wine=%d", total, wine);
    report("root-wine-devices", wine >= 2, detail);
}

int main(void)
{
    group("services");
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        char detail[128];
        snprintf(detail, sizeof(detail), "OpenSCManager err=%lu", GetLastError());
        report("scm-open", 0, detail);
        report("PlugPlay", 0, "no SCM");
        report("WineBus", 0, "no SCM");
        report("WineUsb", 0, "no SCM");
    } else {
        report("scm-open", 1, "handle ok");
        check_service(scm, "PlugPlay");
        check_service(scm, "WineBus");
        check_service(scm, "WineUsb");
        CloseServiceHandle(scm);
    }
    group("root-enumeration");
    check_root_enumeration();
    printf(TAG ": RESULT %d\n", failures);
    fflush(stdout);
    return failures ? 1 : 0;
}
