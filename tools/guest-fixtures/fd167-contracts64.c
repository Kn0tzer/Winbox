/* fd167-contracts64.c -- FD-167 guest proof fixture (64-bit).
 *
 * Two groups:
 *
 * 1) msvcp140 throw helpers imported by the Gang Beasts Xbox-services
 *    thunks DLL. Against the stub runtime each call raises the Wine stub
 *    exception 0x80000100 and kills the caller. After the fix each must
 *    throw a real MSVC C++ exception (code 0xe06d7363) whose type and
 *    payload a vectored handler can read:
 *      std::_Xbad_function_call()  -> throws std::bad_function_call
 *      std::_XGetLastError()       -> throws std::system_error carrying
 *                                     GetLastError() + system_category
 *      std::_Throw_C_error(3)      -> throws std::system_error carrying
 *                                     EBUSY + generic_category
 *
 * 2) setupapi SetupDiGetDeviceInterfaceAlias (imported by Mindustry's
 *    steam_api64.dll): build a device info set with one device carrying
 *    interfaces under two class GUIDs; the alias call must find the
 *    second interface, and fail with ERROR_NO_SUCH_DEVICE_INTERFACE for
 *    an unregistered class GUID.
 */
#include <windows.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int failures;
static int checks;

static void report(const char *name, int ok, const char *detail)
{
    checks++;
    if (!ok) failures++;
    printf("fd167-64: %-52s %s %s\n", name, ok ? "ok  " : "FAIL", detail);
    fflush(stdout);
}

static void group(const char *name)
{
    printf("fd167-64: group %s\n", name);
    fflush(stdout);
}

/* ---- MSVC exception capture --------------------------------------------- */
typedef void (WINAPI *THROW0)(void);
typedef void (WINAPI *THROW1)(int);
typedef void *(WINAPI *GET_CAT)(void);

static jmp_buf jb;
static DWORD seen_code;
static char seen_type[128];
static int seen_val;
static void *seen_cat;
static int seen_has_object;

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD *rec = ep->ExceptionRecord;
    if (rec->ExceptionCode == 0xE06D7363 && rec->NumberParameters >= 4) {
        /* x64 _CxxThrowException: params = magic, pExceptionObject,
         * pThrowInfo (RVA-based), image base. */
        char *base = (char *)(ULONG_PTR)rec->ExceptionInformation[3];
        void *obj = (void *)(ULONG_PTR)rec->ExceptionInformation[1];
        int *ti = (int *)(ULONG_PTR)rec->ExceptionInformation[2];
        int *cta = (int *)(base + ti[3]);           /* CatchableTypeArray */
        int *ct = (int *)(base + cta[1]);           /* first CatchableType */
        char *td = base + ct[1];                    /* TypeDescriptor */
        const char *name = td + 16;                 /* after vtable+spare */
        seen_code = rec->ExceptionCode;
        strncpy(seen_type, name, sizeof(seen_type) - 1);
        seen_type[sizeof(seen_type) - 1] = 0;
        seen_has_object = obj != NULL;
        if (obj) {
            /* std::system_error (MSVC x64 ABI, as modelled by Wine):
             * exception base is 24 bytes (vtable, name, do_free+pad),
             * then error_code { int code @24, category ptr @32 }. */
            seen_val = *(int *)((char *)obj + 24);
            seen_cat = *(void **)((char *)obj + 32);
        }
        longjmp(jb, 1);
    }
    if (rec->ExceptionCode == 0x80000100) { /* EXCEPTION_WINE_STUB */
        seen_code = rec->ExceptionCode;
        seen_type[0] = 0;
        longjmp(jb, 2);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* returns 1 = MSVC exception captured, 2 = wine stub, 0 = returned normally */
static int call_capturing0(THROW0 fn)
{
    void *h;
    seen_code = 0; seen_type[0] = 0; seen_val = -1; seen_cat = NULL; seen_has_object = 0;
    h = AddVectoredExceptionHandler(1, veh);
    if (setjmp(jb) == 0) {
        fn();
        RemoveVectoredExceptionHandler(h);
        return 0;
    }
    RemoveVectoredExceptionHandler(h);
    return seen_code == 0xE06D7363 ? 1 : 2;
}

static int call_capturing1(THROW1 fn, int arg)
{
    void *h;
    seen_code = 0; seen_type[0] = 0; seen_val = -1; seen_cat = NULL; seen_has_object = 0;
    h = AddVectoredExceptionHandler(1, veh);
    if (setjmp(jb) == 0) {
        fn(arg);
        RemoveVectoredExceptionHandler(h);
        return 0;
    }
    RemoveVectoredExceptionHandler(h);
    return seen_code == 0xE06D7363 ? 1 : 2;
}

/* error_category vtable slot 1 is name(): a guest can identify the
 * category a thrown system_error carries by calling it, exactly the
 * way real guest code tells system and generic categories apart. */
static const char *category_name(void *cat)
{
    typedef const char *(*NAMEFN)(void *);
    void **vtbl;
    if (!cat) return "<null>";
    vtbl = *(void ***)cat;
    if (!vtbl || !vtbl[1]) return "<novtbl>";
    return ((NAMEFN)vtbl[1])(cat);
}

static void test_msvcp140(HMODULE m)
{
    char d[224];
    THROW0 pBadCall = (THROW0)(void *)GetProcAddress(m, "?_Xbad_function_call@std@@YAXXZ");
    THROW0 pGetLast = (THROW0)(void *)GetProcAddress(m, "?_XGetLastError@std@@YAXXZ");
    THROW1 pThrowC = (THROW1)(void *)GetProcAddress(m, "?_Throw_C_error@std@@YAXH@Z");
    void *system_cat = NULL, *generic_cat = NULL;
    int rc;

    group("msvcp140 throw helpers");
    sprintf(d, "bad=%p getlast=%p throwc=%p", pBadCall, pGetLast, pThrowC);
    report("msvcp140 exports resolve", pBadCall && pGetLast && pThrowC, d);

    rc = call_capturing0(pBadCall);
    sprintf(d, "rc=%d code=%08lx type=%s", rc, (unsigned long)seen_code, seen_type);
    report("_Xbad_function_call throws bad_function_call",
           rc == 1 && strcmp(seen_type, ".?AVbad_function_call@std@@") == 0, d);

    SetLastError(5);
    rc = call_capturing0(pGetLast);
    system_cat = seen_cat;
    sprintf(d, "rc=%d code=%08lx type=%s val=%d cat=%s", rc, (unsigned long)seen_code,
            seen_type, seen_val, category_name(seen_cat));
    report("_XGetLastError throws system_error(5, system)",
           rc == 1 && strcmp(seen_type, ".?AVsystem_error@std@@") == 0 &&
           seen_val == 5 && seen_cat && strcmp(category_name(seen_cat), "system") == 0, d);

    SetLastError(6);
    rc = call_capturing0(pGetLast);
    sprintf(d, "rc=%d val=%d same_cat_singleton=%d", rc, seen_val,
            seen_cat == system_cat);
    report("_XGetLastError again: value follows, category is the singleton",
           rc == 1 && seen_val == 6 && seen_cat == system_cat, d);

    rc = call_capturing1(pThrowC, 3);
    generic_cat = seen_cat;
    sprintf(d, "rc=%d code=%08lx type=%s val=%d (EBUSY=%d) cat=%s distinct=%d", rc,
            (unsigned long)seen_code, seen_type, seen_val, EBUSY,
            category_name(seen_cat), seen_cat != system_cat);
    report("_Throw_C_error(3) throws system_error(EBUSY, generic)",
           rc == 1 && strcmp(seen_type, ".?AVsystem_error@std@@") == 0 &&
           seen_val == EBUSY && seen_cat && seen_cat != system_cat &&
           strcmp(category_name(seen_cat), "generic") == 0, d);

    rc = call_capturing1(pThrowC, 2);
    sprintf(d, "rc=%d val=%d (EAGAIN=%d) same_generic=%d", rc, seen_val, EAGAIN,
            seen_cat == generic_cat);
    report("_Throw_C_error(2) throws system_error(EAGAIN, generic)",
           rc == 1 && seen_val == EAGAIN && seen_cat == generic_cat, d);
}

/* ---- setupapi alias ------------------------------------------------------- */
typedef void *HDEVINFO;
struct devinfo_data { DWORD cbSize; GUID classGuid; DWORD devInst; ULONG_PTR reserved; };
struct iface_data { DWORD cbSize; GUID classGuid; DWORD flags; ULONG_PTR reserved; };

typedef HDEVINFO (WINAPI *SD_CREATELIST)(const GUID *, HWND);
typedef BOOL (WINAPI *SD_CREATEDEV)(HDEVINFO, const WCHAR *, const GUID *, const WCHAR *, HWND, DWORD, struct devinfo_data *);
typedef BOOL (WINAPI *SD_CREATEIFACE)(HDEVINFO, struct devinfo_data *, const GUID *, const WCHAR *, DWORD, struct iface_data *);
typedef BOOL (WINAPI *SD_ALIAS)(HDEVINFO, struct iface_data *, const GUID *, struct iface_data *);

static const GUID class_system = {0x4d36e97d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const GUID guid_a = {0xfd167001, 0x0001, 0x4001, {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}};
static const GUID guid_b = {0xfd167002, 0x0002, 0x4002, {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}};
static const GUID guid_c = {0xfd167003, 0x0003, 0x4003, {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03}};

static void test_setupapi(HMODULE s)
{
    char d[160];
    SD_CREATELIST pCreateList = (SD_CREATELIST)(void *)GetProcAddress(s, "SetupDiCreateDeviceInfoList");
    SD_CREATEDEV pCreateDev = (SD_CREATEDEV)(void *)GetProcAddress(s, "SetupDiCreateDeviceInfoW");
    SD_CREATEIFACE pCreateIface = (SD_CREATEIFACE)(void *)GetProcAddress(s, "SetupDiCreateDeviceInterfaceW");
    SD_ALIAS pAlias = (SD_ALIAS)(void *)GetProcAddress(s, "SetupDiGetDeviceInterfaceAlias");
    HDEVINFO set;
    struct devinfo_data dev;
    struct iface_data ifa, ifb, alias;
    BOOL ok;
    DWORD gle;

    group("setupapi device interface alias");
    sprintf(d, "list=%p dev=%p iface=%p alias=%p", pCreateList, pCreateDev, pCreateIface, pAlias);
    report("setupapi exports resolve", pCreateList && pCreateDev && pCreateIface && pAlias, d);

    set = pCreateList(NULL, NULL);
    sprintf(d, "set=%p gle=%lu", set, (unsigned long)GetLastError());
    report("SetupDiCreateDeviceInfoList", set != INVALID_HANDLE_VALUE, d);
    if (set == INVALID_HANDLE_VALUE) return;

    memset(&dev, 0, sizeof(dev));
    dev.cbSize = sizeof(dev);
    ok = pCreateDev(set, L"BW64FD167Device", &class_system, NULL, NULL, 1 /*DICD_GENERATE_ID*/, &dev);
    sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)GetLastError());
    report("SetupDiCreateDeviceInfoW", ok == TRUE, d);
    if (!ok) return;

    memset(&ifa, 0, sizeof(ifa));
    ifa.cbSize = sizeof(ifa);
    ok = pCreateIface(set, &dev, &guid_a, NULL, 0, &ifa);
    sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)GetLastError());
    report("SetupDiCreateDeviceInterfaceW class A", ok == TRUE, d);
    if (!ok) return;

    memset(&ifb, 0, sizeof(ifb));
    ifb.cbSize = sizeof(ifb);
    ok = pCreateIface(set, &dev, &guid_b, NULL, 0, &ifb);
    sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)GetLastError());
    report("SetupDiCreateDeviceInterfaceW class B", ok == TRUE, d);
    if (!ok) return;

    memset(&alias, 0, sizeof(alias));
    alias.cbSize = sizeof(alias);
    ok = pAlias(set, &ifa, &guid_b, &alias);
    sprintf(d, "ret=%d gle=%lu guid_match=%d", ok, (unsigned long)GetLastError(),
            memcmp(&alias.classGuid, &guid_b, sizeof(GUID)) == 0);
    report("SetupDiGetDeviceInterfaceAlias A->B", ok == TRUE &&
           memcmp(&alias.classGuid, &guid_b, sizeof(GUID)) == 0, d);

    memset(&alias, 0, sizeof(alias));
    alias.cbSize = sizeof(alias);
    SetLastError(0);
    ok = pAlias(set, &ifa, &guid_c, &alias);
    gle = GetLastError();
    sprintf(d, "ret=%d gle=%08lx", ok, (unsigned long)gle);
    report("SetupDiGetDeviceInterfaceAlias A->C absent",
           ok == FALSE && gle == 0xE0000225 /*ERROR_NO_SUCH_DEVICE_INTERFACE (setupapi.h: APPLICATION_ERROR_MASK|ERROR_SEVERITY_ERROR|0x225)*/, d);

    memset(&alias, 0, sizeof(alias));
    alias.cbSize = 4;
    SetLastError(0);
    ok = pAlias(set, &ifa, &guid_b, &alias);
    gle = GetLastError();
    sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)gle);
    report("SetupDiGetDeviceInterfaceAlias bad cbSize",
           ok == FALSE && gle == ERROR_INVALID_PARAMETER, d);
}

int main(void)
{
    HMODULE m = LoadLibraryA("msvcp140.dll");
    HMODULE s = LoadLibraryA("setupapi.dll");
    char d[96];
    sprintf(d, "msvcp140=%p setupapi=%p", m, s);
    report("target DLLs load", m && s, d);

#ifndef FD167_SKIP_MSVCP
    if (m) test_msvcp140(m);
#endif
#ifndef FD167_SKIP_SETUPAPI
    if (s) test_setupapi(s);
#endif

    printf("fd167-64: RESULT %d (%d checks)\n", failures, checks);
    fflush(stdout);
    return failures ? 1 : 0;
}
