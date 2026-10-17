#include <windows.h>
#include <pthread.h>

static pthread_once_t test_once = PTHREAD_ONCE_INIT;

void write_str(const char* s) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written, len = 0;
    while (s[len]) len++;
    WriteFile(hOut, s, len, &written, NULL);
}

void __attribute__((used)) test_callback(void) {
    write_str("CALLBACK_RAN\n");
}

void __attribute__((used)) my_entry(void) {
    pthread_once(&test_once, test_callback);
    pthread_once(&test_once, test_callback);
    write_str("DONE\n");
    ExitProcess(0);
}
