#include <windows.h>
#include <pthread.h>

// No CRT - custom entry point
static pthread_once_t test_once = PTHREAD_ONCE_INIT;

void __attribute__((used)) test_callback(void) {
    HANDLE hFile = CreateFileW(L"C:\\tmp\\pthread_result.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        const char* msg = "CALLBACK_RAN\n";
        DWORD written;
        WriteFile(hFile, msg, 13, &written, NULL);
        CloseHandle(hFile);
    }
}

void __attribute__((used)) my_entry(void) {
    HANDLE hFile;
    DWORD written;

    // First call - should run callback
    pthread_once(&test_once, test_callback);

    // Second call - should NOT run callback again
    pthread_once(&test_once, test_callback);

    // Append DONE marker
    hFile = CreateFileW(L"C:\\tmp\\pthread_result.txt", GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(hFile, 0, NULL, FILE_END);
        const char* msg = "DONE\n";
        WriteFile(hFile, msg, 5, &written, NULL);
        CloseHandle(hFile);
    }

    ExitProcess(0);
}
