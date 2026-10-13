#include <windows.h>
#include <stdio.h>
int main() {
    FILE *f = fopen("Z:\\tmp\\dtresult.txt", "w");
    if (f) { fprintf(f, "starting\n"); fflush(f); }
    HDESK hdesk = CreateDesktopW(L"Default", NULL, NULL, 0, GENERIC_ALL, NULL);
    if (f) {
        fprintf(f, "CreateDesktopW returned %p\n", hdesk);
        if (!hdesk) {
            fprintf(f, "GetLastError=%lu\n", GetLastError());
        } else {
            fprintf(f, "SUCCESS\n");
        }
        fclose(f);
    }
    printf("desktoptest: starting\n"); fflush(stdout);
    printf("desktoptest: CreateDesktopW returned %p\n", hdesk); fflush(stdout);
    if (!hdesk) printf("desktoptest: GetLastError=%lu\n", GetLastError()); else printf("desktoptest: SUCCESS\n");
    fflush(stdout);
    return 0;
}
