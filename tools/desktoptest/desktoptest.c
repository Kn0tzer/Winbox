#include <windows.h>
#include <stdio.h>
int main() {
    printf("desktoptest: starting\n"); fflush(stdout);
    HDESK hdesk = CreateDesktopW(L"Default", NULL, NULL, 0, GENERIC_ALL, NULL);
    printf("desktoptest: CreateDesktopW returned %p\n", hdesk); fflush(stdout);
    if (!hdesk) {
        printf("desktoptest: GetLastError=%lu\n", GetLastError()); fflush(stdout);
    } else {
        printf("desktoptest: SUCCESS\n"); fflush(stdout);
    }
    return 0;
}
