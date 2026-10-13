#include <windows.h>

// No CRT - custom entry point, no constructor tables needed
void __attribute__((used)) my_entry(void) {
    // Write result to file (no CRT needed)
    HANDLE hFile = CreateFileW(L"Z:\\tmp\\dtresult3.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    // Call CreateDesktopW
    HDESK hdesk = CreateDesktopW(L"Default", NULL, NULL, 0, GENERIC_ALL, NULL);
    DWORD err = GetLastError();

    // Write result to file
    if (hFile != INVALID_HANDLE_VALUE) {
        char buf[256];
        int i = 0;
        if (hdesk) {
            const char* msg = "CreateDesktopW SUCCESS\n";
            while (msg[i]) { buf[i] = msg[i]; i++; }
        } else {
            const char* p1 = "CreateDesktopW FAILED err=";
            int j = 0;
            while (p1[j]) { buf[i++] = p1[j++]; }
            // Convert err to decimal string
            char rev[16];
            int rlen = 0;
            DWORD tmp = err;
            if (tmp == 0) { rev[rlen++] = '0'; }
            else { while (tmp > 0) { rev[rlen++] = '0' + (tmp % 10); tmp /= 10; } }
            while (rlen > 0) { buf[i++] = rev[--rlen]; }
            buf[i++] = '\n';
        }
        DWORD written;
        WriteFile(hFile, buf, i, &written, NULL);
        CloseHandle(hFile);
    }

    ExitProcess(0);
}
