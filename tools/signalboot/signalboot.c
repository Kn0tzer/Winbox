/* signalboot.c - CRT-free helper to signal \KernelObjects\__wineboot_event
 * Built with: i686-w64-mingw32-gcc -nostdlib -e _start -o signalboot.exe signalboot.c -lkernel32 -lntdll
 * No CRT init runs; _start is the true entry point. */
typedef unsigned short USHORT;
typedef unsigned long ULONG;
typedef unsigned long DWORD;
typedef int BOOL;
typedef long LONG;
typedef void *HANDLE;
typedef void *PVOID;
typedef unsigned short WCHAR;
typedef WCHAR *PWSTR;
typedef struct _UNICODE_STRING { USHORT Length; USHORT MaximumLength; PWSTR Buffer; } UNICODE_STRING, *PUNICODE_STRING;
typedef struct _OBJECT_ATTRIBUTES { ULONG Length; HANDLE RootDirectory; PUNICODE_STRING ObjectName; ULONG Attributes; PVOID SecurityDescriptor; PVOID SecurityQualityOfService; } OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;
#define OBJ_CASE_INSENSITIVE 0x00000040L
#define EVENT_MODIFY_STATE 0x0002
#define SYNCHRONIZE 0x00100000L
#define STD_OUTPUT_HANDLE ((DWORD)-11)
__declspec(dllimport) LONG __stdcall NtOpenEvent(HANDLE *EventHandle, DWORD DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes);
__declspec(dllimport) LONG __stdcall NtSetEvent(HANDLE EventHandle, LONG *PreviousState);
__declspec(dllimport) LONG __stdcall NtClose(HANDLE Handle);
__declspec(dllimport) void __stdcall RtlInitUnicodeString(PUNICODE_STRING DestinationString, const WCHAR *SourceString);
__declspec(dllimport) HANDLE __stdcall GetStdHandle(DWORD nStdHandle);
__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE hFile, const void *lpBuffer, DWORD nNumberOfBytesToWrite, DWORD *lpNumberOfBytesWritten, PVOID lpOverlapped);
__declspec(dllimport) void __stdcall ExitProcess(DWORD uExitCode);
static void log_msg(const char *s) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != (HANDLE)-1) {
        DWORD written = 0; DWORD len = 0;
        while (s[len]) len++;
        WriteFile(hOut, s, len, &written, 0);
    }
}
static void log_hex(const char *label, ULONG val) {
    char buf[64]; int i = 0; const char *h = "0123456789ABCDEF";
    while (label[i] && i < 40) { buf[i] = label[i]; i++; }
    buf[i++] = '0'; buf[i++] = 'x'; int sh;
    for (sh = 28; sh >= 0; sh -= 4) buf[i++] = h[(val >> sh) & 0xF];
    buf[i++] = '\r'; buf[i++] = '\n'; buf[i] = 0;
    log_msg(buf);
}
void _start(void) {
    UNICODE_STRING uname; OBJECT_ATTRIBUTES oa; HANDLE hEvent = 0; LONG status;
    static const WCHAR evpath[] = { '\\','K','e','r','n','e','l','O','b','j','e','c','t','s','\\', '_','_','w','i','n','e','b','o','o','t','_','e','v','e','n','t',0 };
    log_msg("signalboot: opening \\KernelObjects\\__wineboot_event\r\n");
    RtlInitUnicodeString(&uname, evpath);
    oa.Length = sizeof(oa); oa.RootDirectory = 0; oa.ObjectName = &uname;
    oa.Attributes = OBJ_CASE_INSENSITIVE; oa.SecurityDescriptor = 0; oa.SecurityQualityOfService = 0;
    status = NtOpenEvent(&hEvent, EVENT_MODIFY_STATE | SYNCHRONIZE, &oa);
    if (status != 0) {
        log_msg("signalboot: NtOpenEvent FAILED\r\n");
        log_hex("signalboot: status=", (ULONG)status);
        ExitProcess(2);
    }
    log_msg("signalboot: NtOpenEvent OK\r\n");
    { LONG prev = -1; status = NtSetEvent(hEvent, &prev);
      if (status != 0) {
        log_msg("signalboot: NtSetEvent FAILED\r\n");
        log_hex("signalboot: status=", (ULONG)status);
        NtClose(hEvent); ExitProcess(3);
      }
      log_msg("signalboot: NtSetEvent OK, event signaled\r\n"); }
    NtClose(hEvent);
    log_msg("signalboot: done\r\n");
    ExitProcess(0);
}
