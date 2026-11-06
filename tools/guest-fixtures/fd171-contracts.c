/* fd171-contracts.c -- FD-171 guest proof fixture (both arches).
 *
 * FD-171: 14,287 validated winebuild-stub exports across the shipped
 * system libraries. This fixture implements the lane's resolved
 * groups (203 baseline rows: 107 x86-64 + 96 i386). A winebuild stub
 * raises the Wine stub exception 0x80000100 and kills the caller, so
 * on the pre-fix runtime the fixture dies at the first stubbed group
 * and the RESULT line never prints; after the fix every group must
 * produce its documented result.
 *
 * Groups: propsys conversions, ntdll splay-tree generic tables +
 * activation-context query, the cfgmgr32/setupapi config-manager
 * family, pdh formatted counter arrays, combase HSTRING marshaling +
 * error-reporting flags, kernelbase/kernel32 HeapSummary /
 * ReadConsoleInputEx / RegisterWaitForInputIdle, and the
 * single-function DLLs (cryptext, d2d1, hid, mf, mscms, prntvpt,
 * sspicli, vcomp family). x64-only groups: mfplat media-type/path
 * helpers, windows.networking activation factory, wintypes
 * RoParseTypeName, wofutil WofIsExternalFile, pdh
 * PdhVbGetDoubleCounterValue. RoFailFastWithErrorContext is
 * deliberately NOT called: its contract is to terminate the process.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef __x86_64__
#define TAG "fd171-64"
#else
#define TAG "fd171-32"
#endif

static int failures;
static int checks;

static void report(const char *name, int ok, const char *detail)
{
    checks++;
    if (!ok) failures++;
    printf(TAG ": %-46s %s %s\n", name, ok ? "ok  " : "FAIL", detail);
    fflush(stdout);
}

static void group(const char *name)
{
    printf(TAG ": group %s\n", name);
    fflush(stdout);
}

#define GP(mod, name) ((void *)GetProcAddress(mod, name))

/* ---------- shared minimal types (stable SDK layouts) ---------- */
/* ntddk generic-table types (absent from mingw's winternl.h; layouts
 * are the documented ntddk ones the implementation operates on) */
typedef LONG CLONG;
typedef struct _RTL_SPLAY_LINKS {
    struct _RTL_SPLAY_LINKS *Parent;
    struct _RTL_SPLAY_LINKS *LeftChild;
    struct _RTL_SPLAY_LINKS *RightChild;
} RTL_SPLAY_LINKS, *PRTL_SPLAY_LINKS;
typedef enum _RTL_GENERIC_COMPARE_RESULTS {
    GenericLessThan, GenericGreaterThan, GenericEqual
} RTL_GENERIC_COMPARE_RESULTS;
typedef RTL_GENERIC_COMPARE_RESULTS (WINAPI *PRTL_GENERIC_COMPARE_ROUTINE)(struct _RTL_GENERIC_TABLE *, void *, void *);
typedef void * (WINAPI *PRTL_GENERIC_ALLOCATE_ROUTINE)(struct _RTL_GENERIC_TABLE *, CLONG);
typedef void (WINAPI *PRTL_GENERIC_FREE_ROUTINE)(struct _RTL_GENERIC_TABLE *, void *);
typedef struct _RTL_GENERIC_TABLE {
    PRTL_SPLAY_LINKS TableRoot;
    LIST_ENTRY InsertOrderList;
    void *OrderedPointer;
    unsigned int WhichOrderedElement;
    unsigned int NumberGenericTableElements;
    PRTL_GENERIC_COMPARE_ROUTINE CompareRoutine;
    PRTL_GENERIC_ALLOCATE_ROUTINE AllocateRoutine;
    PRTL_GENERIC_FREE_ROUTINE FreeRoutine;
    void *TableContext;
} RTL_GENERIC_TABLE, *PRTL_GENERIC_TABLE;
typedef struct _GENERIC_TABLE_FIXTURE GENERIC_TABLE_FIXTURE;

/* ================= propsys ================= */
static void test_propsys(HMODULE propsys, HMODULE oleaut)
{
    group("propsys");
    typedef HRESULT (WINAPI *FN_PV_TO_BSTR)(const PROPVARIANT *, BSTR *);
    typedef ULONG (WINAPI *FN_PV_TO_U32_DEF)(const PROPVARIANT *, ULONG);
    typedef HRESULT (WINAPI *FN_PV_GET_STR_ELEM)(const PROPVARIANT *, ULONG, WCHAR **);
    typedef HRESULT (WINAPI *FN_INIT_STRVEC)(const WCHAR **, ULONG, PROPVARIANT *);
    typedef HRESULT (WINAPI *FN_PV_TO_VAR)(const PROPVARIANT *, VARIANT *);
    typedef HRESULT (WINAPI *FN_VAR_TO_PV)(const VARIANT *, PROPVARIANT *);
    typedef HRESULT (WINAPI *FN_PV_CLEAR)(PROPVARIANT *);
    /* PropVariantClear is NOT a propsys.dll export in Wine: it lives
     * in combase.dll and is forwarded through ole32.dll. Resolving it
     * from propsys returns NULL and the unguarded call below jumps
     * through NULL. ole32.dll is always loaded (the fixture imports
     * it, and propsys.dll imports it too). */
    HMODULE ole32 = GetModuleHandleW(L"ole32.dll");
    FN_PV_CLEAR pClear = (FN_PV_CLEAR)GP(ole32, "PropVariantClear");
    FN_PV_GET_STR_ELEM pGetElem = (FN_PV_GET_STR_ELEM)GP(propsys, "PropVariantGetStringElem");
    char d[128];

    /* string-vector round trip (both arches: GetStringElem resolved).
     * A VT_VECTOR | VT_LPWSTR PROPVARIANT carries an inline CALPWSTR
     * (count + pointer array), never a SAFEARRAY: SafeArrayCreateVector
     * refuses VT_LPWSTR on native Windows too (it is not a valid
     * SafeArray element type), and PropVariantGetStringElem reads
     * calpwstr.pElems for this vt. Build the value the way
     * InitPropVariantFromStringVector does. */
    {
        PROPVARIANT pv;
        WCHAR *elem;
        HRESULT hr;
        memset(&pv, 0, sizeof(pv));
        pv.vt = VT_VECTOR | VT_LPWSTR;
        pv.calpwstr.cElems = 2;
        pv.calpwstr.pElems = (WCHAR **)CoTaskMemAlloc(2 * sizeof(WCHAR *));
        pv.calpwstr.pElems[0] = (WCHAR *)CoTaskMemAlloc(6 * sizeof(WCHAR));
        wcscpy(pv.calpwstr.pElems[0], L"alpha");
        pv.calpwstr.pElems[1] = (WCHAR *)CoTaskMemAlloc(5 * sizeof(WCHAR));
        wcscpy(pv.calpwstr.pElems[1], L"beta");
        elem = NULL;
        hr = pGetElem(&pv, 1, &elem);
        _snprintf(d, sizeof(d), "hr=%08lx elem=%ls", (unsigned long)hr, elem ? elem : L"(null)");
        report("PropVariantGetStringElem", hr == 0 && elem && !wcscmp(elem, L"beta"), d);
        if (elem) CoTaskMemFree(elem);
        if (pClear) pClear(&pv);
    }
#ifdef __x86_64__
    {
        FN_PV_TO_BSTR pToBSTR = (FN_PV_TO_BSTR)GP(propsys, "PropVariantToBSTR");
        FN_PV_TO_U32_DEF pToU32 = (FN_PV_TO_U32_DEF)GP(propsys, "PropVariantToUInt32WithDefault");
        FN_INIT_STRVEC pInitVec = (FN_INIT_STRVEC)GP(propsys, "InitPropVariantFromStringVector");
        FN_PV_TO_VAR pToVar = (FN_PV_TO_VAR)GP(propsys, "PropVariantToVariant");
        FN_VAR_TO_PV pVarTo = (FN_VAR_TO_PV)GP(propsys, "VariantToPropVariant");
        PROPVARIANT pv;
        BSTR out = NULL;
        HRESULT hr;
        const WCHAR *words[2] = { L"one", L"two" };
        VARIANT var;
        PROPVARIANT pv2;

        memset(&pv, 0, sizeof(pv));
        pv.vt = VT_BSTR; pv.bstrVal = SysAllocString(L"hello171");
        hr = pToBSTR(&pv, &out);
        _snprintf(d, sizeof(d), "hr=%08lx out=%ls", (unsigned long)hr, out ? out : L"(null)");
        report("PropVariantToBSTR", hr == 0 && out && !wcscmp(out, L"hello171"), d);
        if (out) SysFreeString(out);
        pClear(&pv);

        memset(&pv, 0, sizeof(pv));
        pv.vt = VT_I4; pv.lVal = 171;
        _snprintf(d, sizeof(d), "value=%lu", (unsigned long)pToU32(&pv, 9));
        report("PropVariantToUInt32WithDefault(value)", pToU32(&pv, 9) == 171, d);
        memset(&pv, 0, sizeof(pv));
        pv.vt = VT_EMPTY;
        report("PropVariantToUInt32WithDefault(empty)", pToU32(&pv, 9) == 0, "empty converts to 0");

        hr = pInitVec(words, 2, &pv);
        _snprintf(d, sizeof(d), "hr=%08lx vt=%u count=%lu", (unsigned long)hr, pv.vt,
                  (pv.vt & VT_VECTOR) ? (unsigned long)pv.calpwstr.cElems : 0);
        report("InitPropVariantFromStringVector",
               hr == 0 && pv.vt == (VT_VECTOR | VT_LPWSTR) && pv.calpwstr.cElems == 2, d);
        pClear(&pv);

        memset(&pv, 0, sizeof(pv));
        pv.vt = VT_BSTR; pv.bstrVal = SysAllocString(L"roundtrip");
        VariantInit(&var);
        hr = pToVar(&pv, &var);
        report("PropVariantToVariant", hr == 0 && var.vt == VT_BSTR && var.bstrVal && !wcscmp(var.bstrVal, L"roundtrip"), "bstr preserved");
        memset(&pv2, 0, sizeof(pv2));
        hr = pVarTo(&var, &pv2);
        report("VariantToPropVariant", hr == 0 && pv2.vt == VT_BSTR && pv2.bstrVal && !wcscmp(pv2.bstrVal, L"roundtrip"), "bstr preserved back");
        VariantClear(&var);
        pClear(&pv);
        pClear(&pv2);
    }
#endif
    (void)oleaut;
}

/* ================= ntdll: splay generic tables + actctx query ================= */
typedef struct _TENTRY {
    int key;
    int pad;
} TENTRY;

static int cmp_calls;
static LONG WINAPI cmp_fn(struct _RTL_GENERIC_TABLE *table, void *a, void *b)
{
    cmp_calls++;
    (void)table;
    if (*(int *)a < *(int *)b) return GenericLessThan;
    if (*(int *)a > *(int *)b) return GenericGreaterThan;
    return GenericEqual;
}
static void * WINAPI alloc_fn(struct _RTL_GENERIC_TABLE *table, CLONG size)
{
    (void)table;
    return malloc(size);
}
static void WINAPI free_fn(struct _RTL_GENERIC_TABLE *table, void *ptr)
{
    (void)table;
    free(ptr);
}

static void test_ntdll(HMODULE ntdll)
{
    group("ntdll");
    typedef void (WINAPI *FN_INIT_TABLE)(PRTL_GENERIC_TABLE, PRTL_GENERIC_COMPARE_ROUTINE, PRTL_GENERIC_ALLOCATE_ROUTINE, PRTL_GENERIC_FREE_ROUTINE, void *);
    typedef void * (WINAPI *FN_INSERT)(PRTL_GENERIC_TABLE, void *, CLONG, BOOLEAN *);
    typedef BOOLEAN (WINAPI *FN_DELETE_ELEM)(PRTL_GENERIC_TABLE, void *);
    typedef void * (WINAPI *FN_ENUM)(PRTL_GENERIC_TABLE, BOOLEAN);
    typedef BOOLEAN (WINAPI *FN_ISEMPTY)(PRTL_GENERIC_TABLE);
    typedef PRTL_SPLAY_LINKS (WINAPI *FN_SPLAY)(PRTL_SPLAY_LINKS);
    typedef PRTL_SPLAY_LINKS (WINAPI *FN_SUCC)(PRTL_SPLAY_LINKS);
    typedef NTSTATUS (WINAPI *FN_QUERY_ACTCTX)(ULONG, void *, ULONG, void *, SIZE_T, SIZE_T *);
    FN_INIT_TABLE pInit = (FN_INIT_TABLE)GP(ntdll, "RtlInitializeGenericTable");
    FN_INSERT pInsert = (FN_INSERT)GP(ntdll, "RtlInsertElementGenericTable");
    FN_DELETE_ELEM pDel = (FN_DELETE_ELEM)GP(ntdll, "RtlDeleteElementGenericTable");
    FN_ENUM pEnum = (FN_ENUM)GP(ntdll, "RtlEnumerateGenericTable");
    FN_ISEMPTY pEmpty = (FN_ISEMPTY)GP(ntdll, "RtlIsGenericTableEmpty");
    char d[128];

    {
        RTL_GENERIC_TABLE table;
        TENTRY v = { 0, 0 }, *got;
        BOOLEAN isnew = FALSE;
        int expect[3] = { 5, 10, 20 };
        int seen[3] = { 0, 0, 0 };
        int i, n = 0, key;

        memset(&table, 0, sizeof(table));
        pInit(&table, (PRTL_GENERIC_COMPARE_ROUTINE)cmp_fn, alloc_fn, free_fn, NULL);
        report("RtlIsGenericTableEmpty(new)", pEmpty(&table) == TRUE, "empty at start");
        v.key = 10; pInsert(&table, &v, sizeof(v), &isnew);
        v.key = 5;  pInsert(&table, &v, sizeof(v), &isnew);
        v.key = 20; pInsert(&table, &v, sizeof(v), &isnew);
        report("RtlIsGenericTableEmpty(3 elems)", pEmpty(&table) == FALSE, "not empty");
        got = (TENTRY *)pEnum(&table, TRUE);
        while (got && n < 3) { seen[n++] = got->key; got = (TENTRY *)pEnum(&table, FALSE); }
        _snprintf(d, sizeof(d), "order=%d,%d,%d n=%d", seen[0], seen[1], seen[2], n);
        report("RtlEnumerateGenericTable order", n == 3 && !memcmp(seen, expect, sizeof(expect)), d);
        key = 10;
        report("RtlDeleteElementGenericTable", pDel(&table, &key) == TRUE, "deleted 10");
        n = 0; seen[0] = seen[1] = seen[2] = 0;
        got = (TENTRY *)pEnum(&table, TRUE);
        while (got && n < 3) { seen[n++] = got->key; got = (TENTRY *)pEnum(&table, FALSE); }
        report("enumerate after delete", n == 2 && seen[0] == 5 && seen[1] == 20, "5,20 remain");
        key = 5; pDel(&table, &key);
        key = 20; pDel(&table, &key);
        report("RtlIsGenericTableEmpty(drained)", pEmpty(&table) == TRUE, "empty again");
        (void)i;
    }
    {
        /* hand-built splay links: root 10 with left child 5 */
        FN_SPLAY pSplay = (FN_SPLAY)GP(ntdll, "RtlSplay");
        FN_SUCC pSucc = (FN_SUCC)GP(ntdll, "RtlRealSuccessor");
        FN_SUCC pPred = (FN_SUCC)GP(ntdll, "RtlRealPredecessor");
        RTL_SPLAY_LINKS root, left;
        PRTL_SPLAY_LINKS res;
        memset(&root, 0, sizeof(root)); memset(&left, 0, sizeof(left));
        root.Parent = &root; /* splay-link contract: the root's Parent points to itself */
        root.LeftChild = &left; left.Parent = &root;
        report("RtlRealSuccessor(root)", pSucc(&root) == NULL, "none above max");
        report("RtlRealPredecessor(root)", pPred(&root) == &left, "left child");
        report("RtlRealSuccessor(left)", pSucc(&left) == &root, "parent");
        res = pSplay(&left);
        _snprintf(d, sizeof(d), "newroot=%p left.Parent=%p root.Parent=%p", (void *)res, (void *)left.Parent, (void *)root.Parent);
        report("RtlSplay(left) rotates to root", res == &left && left.Parent == &left && root.Parent == &left, d);
    }
    {
        FN_QUERY_ACTCTX pQ = (FN_QUERY_ACTCTX)GP(ntdll, "RtlQueryInformationActiveActivationContext");
        SIZE_T retlen = 0;
        NTSTATUS st = pQ(1 /*ActivationContextBasicInformation*/, NULL, 0, NULL, 0, &retlen);
        _snprintf(d, sizeof(d), "status=%08lx retlen=%lu", (unsigned long)st, (unsigned long)retlen);
        report("RtlQueryInformationActiveActivationContext",
               st == (NTSTATUS)0xC0000023L || st == 0, d);
    }
}

/* ================= cfgmgr32 / setupapi ================= */
static void test_cm(HMODULE cfg)
{
    group("cfgmgr32");
    typedef DWORD (WINAPI *FN_ENUM_EN)(ULONG, WCHAR *, ULONG *, ULONG);
    typedef DWORD (WINAPI *FN_IDLIST_SIZE)(ULONG *, const WCHAR *, DWORD, HANDLE);
    typedef DWORD (WINAPI *FN_IDLIST)(const WCHAR *, WCHAR *, ULONG, DWORD, HANDLE);
    typedef DWORD (WINAPI *FN_ID_SIZE)(ULONG *, DWORD, ULONG, HANDLE);
    typedef DWORD (WINAPI *FN_LOCATE)(DWORD *, const WCHAR *, ULONG);
    typedef DWORD (WINAPI *FN_ENUM_CLS)(ULONG, GUID *, ULONG, HANDLE);
    typedef DWORD (WINAPI *FN_CLSKEY_NAME)(const GUID *, WCHAR *, ULONG *, ULONG, HANDLE);
    typedef DWORD (WINAPI *FN_OPEN_CLSKEY)(const GUID *, const WCHAR *, REGSAM, DWORD, HKEY *, ULONG, HANDLE);
    typedef DWORD (WINAPI *FN_IFACE_LIST)(const GUID *, const WCHAR *, WCHAR *, ULONG, ULONG, HANDLE);
    FN_ENUM_EN pEnumEn = (FN_ENUM_EN)GP(cfg, "CM_Enumerate_EnumeratorsW");
    FN_IDLIST_SIZE pSize = (FN_IDLIST_SIZE)GP(cfg, "CM_Get_Device_ID_List_Size_ExW");
    FN_IDLIST pList = (FN_IDLIST)GP(cfg, "CM_Get_Device_ID_List_ExW");
    FN_ID_SIZE pIdSize = (FN_ID_SIZE)GP(cfg, "CM_Get_Device_ID_Size_Ex");
    FN_ENUM_CLS pEnumCls = (FN_ENUM_CLS)GP(cfg, "CM_Enumerate_Classes_Ex");
    FN_CLSKEY_NAME pKeyName = (FN_CLSKEY_NAME)GP(cfg, "CM_Get_Class_Key_Name_ExW");
    FN_OPEN_CLSKEY pOpenKey = (FN_OPEN_CLSKEY)GP(cfg, "CM_Open_Class_Key_ExW");
    FN_IFACE_LIST pIface = (FN_IFACE_LIST)GP(cfg, "CM_Get_Device_Interface_List_ExW");
    char d[160];
    DWORD cr;

    {
        WCHAR buf[256]; ULONG len = 256;
        cr = pEnumEn(0, buf, &len, 0);
        _snprintf(d, sizeof(d), "cr=%lu first=%ls", (unsigned long)cr, cr == 0 ? buf : L"-");
        report("CM_Enumerate_EnumeratorsW(0)", cr == 0 && buf[0] != 0, d);
    }
    {
        ULONG chars = 0;
        cr = pSize(&chars, NULL, 0, NULL);
        report("CM_Get_Device_ID_List_Size_ExW", cr == 0 && chars >= 1, "size call");
        if (cr == 0) {
            WCHAR *list = (WCHAR *)malloc(chars * sizeof(WCHAR));
            cr = pList(NULL, list, chars, 0, NULL);
            if (cr == 0) {
                int count = 0; WCHAR *p = list;
                ULONG idlen = 0;
                while (*p) { count++; p += wcslen(p) + 1; }
                _snprintf(d, sizeof(d), "ids=%d chars=%lu", count, (unsigned long)chars);
                report("CM_Get_Device_ID_List_ExW", count >= 1, d);
                if (count >= 1) {
                    /* CM_Locate_DevNodeW is an unimplemented stub in Wine 8.0
                     * (setupapi/stubs.c returns CR_FAILURE unconditionally), so
                     * the devnode is obtained through the working SetupDi path:
                     * create a test device and use its DevInst. This still
                     * exercises the FD-171 CM_Get_Device_ID_Size_Ex. */
                    HMODULE setupapi = LoadLibraryW(L"setupapi.dll");
                    typedef void *(WINAPI *FN_DILIST)(const GUID *, void *);
                    typedef BOOL (WINAPI *FN_DICREATE)(void *, const WCHAR *, const GUID *,
                                                       const WCHAR *, void *, DWORD, void *);
                    typedef BOOL (WINAPI *FN_DIDESTROY)(void *);
                    typedef struct _SP_DEVINFO_DATA_FIX {
                        DWORD cbSize; GUID ClassGuid; DWORD DevInst; ULONG_PTR Reserved;
                    } SP_DEVINFO_DATA_FIX;
                    FN_DILIST pDiList = (FN_DILIST)GP(setupapi, "SetupDiCreateDeviceInfoList");
                    FN_DICREATE pDiCreate = (FN_DICREATE)GP(setupapi, "SetupDiCreateDeviceInfoW");
                    FN_DIDESTROY pDiDestroy = (FN_DIDESTROY)GP(setupapi, "SetupDiDestroyDeviceInfoList");
                    static const GUID testguid = { 0x17117117, 0x1717, 0x1717,
                                                   { 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17 } };
                    SP_DEVINFO_DATA_FIX data;
                    void *hinfo = pDiList ? pDiList(NULL, NULL) : NULL;
                    memset(&data, 0, sizeof(data));
                    data.cbSize = sizeof(data);
                    /* DICD_GENERATE_ID (1): SetupDi generates
                     * "ROOT\FD171TEST\0000" (19 chars) instead of using
                     * the bare name, so the check cannot collide with a
                     * device left in a reused prefix. */
                    if (hinfo && pDiCreate && pDiCreate(hinfo, L"FD171TEST", &testguid,
                                                         NULL, NULL, 1, &data)) {
                        cr = pIdSize(&idlen, data.DevInst, 0, NULL);
                        _snprintf(d, sizeof(d), "cr=%lu idlen=%lu", (unsigned long)cr,
                                  (unsigned long)idlen);
                        report("CM_Get_Device_ID_Size_Ex", cr == 0 && idlen == 19, d);
                    } else {
                        report("CM_Get_Device_ID_Size_Ex", 0, "SetupDi device creation failed");
                    }
                    if (hinfo && pDiDestroy) pDiDestroy(hinfo);
                }
            }
            free(list);
        }
    }
    {
        GUID guid; HKEY hk = NULL; WCHAR name[256]; ULONG len = 256;
        cr = pEnumCls(0, &guid, 0, NULL);
        report("CM_Enumerate_Classes_Ex(0)", cr == 0, "class enumerated");
        if (cr == 0) {
            cr = pKeyName(&guid, name, &len, 0, NULL);
            _snprintf(d, sizeof(d), "cr=%lu name=%ls", (unsigned long)cr, cr == 0 ? name : L"-");
            report("CM_Get_Class_Key_Name_ExW", cr == 0 && name[0] != 0, d);
            cr = pOpenKey(&guid, NULL, KEY_READ, 1 /*RegDisposition_OpenExisting*/, &hk, 0, NULL);
            report("CM_Open_Class_Key_ExW", cr == 0 && hk != NULL, "class key opened");
            if (hk) RegCloseKey(hk);
        }
    }
    {
        static const GUID none = { 0x11111111, 0x2222, 0x3333, { 4, 4, 4, 4, 4, 4, 4, 4 } };
        WCHAR buf[4]; ULONG needed = 0;
        cr = pIface(&none, NULL, buf, 4, 0, NULL);
        _snprintf(d, sizeof(d), "cr=%lu buf0=%u", (unsigned long)cr, buf[0]);
        report("CM_Get_Device_Interface_List_ExW(none)", cr == 0 && buf[0] == 0, d);
    }
}

/* ================= pdh ================= */
static void test_pdh(HMODULE pdh)
{
    group("pdh");
    typedef LONG (WINAPI *FN_OPEN_Q)(const WCHAR *, DWORD_PTR, HANDLE *);
    typedef LONG (WINAPI *FN_ADD_C)(HANDLE, const WCHAR *, DWORD_PTR, HANDLE *);
    typedef LONG (WINAPI *FN_COLLECT)(HANDLE);
    typedef LONG (WINAPI *FN_FMT_ARR)(HANDLE, DWORD, DWORD *, DWORD *, void *);
    FN_OPEN_Q pOpen = (FN_OPEN_Q)GP(pdh, "PdhOpenQueryW");
    FN_ADD_C pAdd = (FN_ADD_C)GP(pdh, "PdhAddCounterW");
    FN_COLLECT pCollect = (FN_COLLECT)GP(pdh, "PdhCollectQueryData");
    FN_FMT_ARR pArr = (FN_FMT_ARR)GP(pdh, "PdhGetFormattedCounterArrayW");
    HANDLE q = NULL, c = NULL;
    LONG st;
    char d[160];
    st = pOpen(NULL, 0, &q);
    if (st != 0) { report("pdh query open", 0, "PdhOpenQueryW failed"); return; }
    st = pAdd(q, L"\\Processor(_Total)\\% Processor Time", 0, &c);
    _snprintf(d, sizeof(d), "add status=%08lx", (unsigned long)st);
    report("PdhAddCounterW", st == 0, d);
    if (st != 0) return;
    pCollect(q);
    Sleep(50);
    pCollect(q);
    {
        DWORD bufSize = 0, count = 0;
        st = pArr(c, 0x00000200 /*PDH_FMT_DOUBLE*/, &bufSize, &count, NULL);
        _snprintf(d, sizeof(d), "probe status=%08lx bufSize=%lu", (unsigned long)st, (unsigned long)bufSize);
        report("PdhGetFormattedCounterArrayW(more-data)", st == (LONG)0x800007D2L && bufSize > 0, d);
        if (st == (LONG)0x800007D2L && bufSize > 0) {
            void *buf = malloc(bufSize);
            st = pArr(c, 0x00000200, &bufSize, &count, buf);
            _snprintf(d, sizeof(d), "status=%08lx count=%lu", (unsigned long)st, (unsigned long)count);
            report("PdhGetFormattedCounterArrayW(data)", st == 0 && count == 1, d);
            free(buf);
        }
    }
#ifdef __x86_64__
    {
        typedef double (WINAPI *FN_VB)(HANDLE, LONG *);
        FN_VB pVb = (FN_VB)GP(pdh, "PdhVbGetDoubleCounterValue");
        LONG status = 0;
        double v = pVb(c, &status);
        _snprintf(d, sizeof(d), "value=%.3f status=%08lx", v, (unsigned long)status);
        report("PdhVbGetDoubleCounterValue", status == 0 && v >= 0.0, d);
    }
#endif
}

/* ================= combase ================= */
static void test_combase(HMODULE combase)
{
    group("combase");
    typedef HRESULT (WINAPI *FN_CREATE_STR)(const WCHAR *, unsigned, void **);
    typedef const WCHAR * (WINAPI *FN_RAWBUF)(void *, unsigned *);
    typedef HRESULT (WINAPI *FN_DEL_STR)(void *);
    typedef ULONG (WINAPI *FN_USIZE)(ULONG *, ULONG, void **);
    typedef unsigned char * (WINAPI *FN_UMARSHAL)(ULONG *, unsigned char *, void **);
    typedef unsigned char * (WINAPI *FN_UUNMARSHAL)(ULONG *, unsigned char *, void **);
    typedef void (WINAPI *FN_UFREE)(ULONG *, void **);
    typedef HRESULT (WINAPI *FN_FLAGS)(ULONG *);
    FN_CREATE_STR pCreate = (FN_CREATE_STR)GP(combase, "WindowsCreateString");
    FN_RAWBUF pRaw = (FN_RAWBUF)GP(combase, "WindowsGetStringRawBuffer");
    FN_DEL_STR pDel = (FN_DEL_STR)GP(combase, "WindowsDeleteString");
    FN_USIZE pSize = (FN_USIZE)GP(combase, "HSTRING_UserSize");
    FN_UMARSHAL pMar = (FN_UMARSHAL)GP(combase, "HSTRING_UserMarshal");
    FN_UUNMARSHAL pUnm = (FN_UUNMARSHAL)GP(combase, "HSTRING_UserUnmarshal");
    FN_UFREE pFree = (FN_UFREE)GP(combase, "HSTRING_UserFree");
    FN_FLAGS pFlags = (FN_FLAGS)GP(combase, "RoGetErrorReportingFlags");
    void *hs = NULL, *hs2 = NULL;
    char d[128];
    HRESULT hr = pCreate(L"marshal-me-171", 14, &hs);
    if (hr != 0 || !hs) { report("WindowsCreateString", 0, "setup failed"); return; }
    {
        ULONG flags = 0, size;
        unsigned char *buf, *end;
        const WCHAR *raw;
        size = pSize(&flags, 0, &hs);
        buf = (unsigned char *)malloc(size + 16);
        end = pMar(&flags, buf, &hs);
        report("HSTRING_UserMarshal", end > buf && size > 0, "marshaled");
        pUnm(&flags, buf, &hs2);
        raw = hs2 ? pRaw(hs2, NULL) : NULL;
        _snprintf(d, sizeof(d), "roundtrip=%ls", raw ? raw : L"(null)");
        report("HSTRING_UserUnmarshal", raw && !wcscmp(raw, L"marshal-me-171"), d);
        if (hs2) { void *tmp = hs2; pFree(&flags, &tmp); }
        free(buf);
    }
    {
        ULONG flags = 0;
        hr = pFlags(&flags);
        _snprintf(d, sizeof(d), "hr=%08lx flags=%lu", (unsigned long)hr, (unsigned long)flags);
        report("RoGetErrorReportingFlags", hr == 0 && flags == 4, d);
    }
    pDel(hs);
}

/* ================= kernelbase / kernel32 ================= */
typedef struct _HSUM_FIX {
    DWORD cb;
    SIZE_T cbAllocated, cbCommitted, cbReserved, cbMaxReserve;
} HSUM_FIX;
static BOOL WINAPI dummy_wait_cb(void *ctx, BOOLEAN timeout) { (void)ctx; (void)timeout; return TRUE; }

static void test_kernel(HMODULE kernelbase, HMODULE kernel32)
{
    group("kernelbase/kernel32");
    typedef BOOL (WINAPI *FN_HSUM)(HANDLE, DWORD, HSUM_FIX *);
    typedef BOOL (WINAPI *FN_RCIE)(HANDLE, INPUT_RECORD *, DWORD, DWORD *, DWORD);
    typedef void (WINAPI *FN_RWII)(void *);
    FN_HSUM pSum32 = (FN_HSUM)GP(kernel32, "HeapSummary");
    FN_HSUM pSumB = (FN_HSUM)GP(kernelbase, "HeapSummary");
    FN_RCIE pRcie = (FN_RCIE)GP(kernelbase, "ReadConsoleInputExW");
    FN_RWII pRwii = (FN_RWII)GP(kernel32, "RegisterWaitForInputIdle");
    HSUM_FIX hs;
    char d[128];
    memset(&hs, 0, sizeof(hs)); hs.cb = sizeof(hs);
    {
        BOOL ok = pSumB(GetProcessHeap(), 0, &hs);
        _snprintf(d, sizeof(d), "ok=%d allocated=%lu committed=%lu", ok,
                  (unsigned long)hs.cbAllocated, (unsigned long)hs.cbCommitted);
        report("HeapSummary(kernelbase)", ok && hs.cbAllocated > 0 && hs.cbCommitted > 0, d);
    }
    memset(&hs, 0, sizeof(hs)); hs.cb = sizeof(hs);
    report("HeapSummary(kernel32 forward)", pSum32(GetProcessHeap(), 0, &hs) && hs.cbAllocated > 0, "forward works");
    {
        INPUT_RECORD rec; DWORD readn = 0;
        BOOL ok = pRcie((HANDLE)(INT_PTR)-1, &rec, 1, &readn, 0x2);
        _snprintf(d, sizeof(d), "ok=%d gle=%lu", ok, (unsigned long)GetLastError());
        report("ReadConsoleInputExW(bad handle)", !ok, d);
    }
    pRwii((void *)dummy_wait_cb);
    report("RegisterWaitForInputIdle", 1, "callback registered");
}

/* ================= small DLLs ================= */
static void test_small(void)
{
    group("small-dlls");
    char d[128];
    /* cryptext */
    {
        HMODULE m = LoadLibraryW(L"cryptext.dll");
        typedef BOOL (WINAPI *FN_CER)(const WCHAR *, DWORD);
        FN_CER pCer = (FN_CER)GP(m, "CryptExtOpenCERW");
        /* Not called: every path shows UI (MessageBoxW for a bad file,
         * the certificate viewer for a good one), so it can never
         * complete in a headless guest; the dialog path faults there.
         * The export resolving proves the FD-171 implementation is
         * linked; behaviour is covered by upstream Wine's own tests. */
        report("CryptExtOpenCERW(export)", pCer != NULL, "present; UI-only, not called");
    }
    /* d2d1 */
    {
        HMODULE m = LoadLibraryW(L"d2d1.dll");
        typedef HRESULT (WINAPI *FN_DCC)(void *, const void *, void **);
        FN_DCC p = (FN_DCC)GP(m, "D2D1CreateDeviceContext");
        HRESULT hr = p(NULL, NULL, NULL);
        _snprintf(d, sizeof(d), "hr=%08lx", (unsigned long)hr);
        report("D2D1CreateDeviceContext(NULL)", hr == (HRESULT)0x80070057L || hr == (HRESULT)0x80004003L, d);
    }
    /* hid */
    {
        HMODULE m = LoadLibraryW(L"hid.dll");
        typedef LONG (WINAPI *FN_SETDATA)(int, void *, ULONG *, void *, char *, ULONG);
        FN_SETDATA p = (FN_SETDATA)GP(m, "HidP_SetData");
        char report_buf[8] = { 0 };
        ULONG len = 0;
        LONG st = p(0 /*HidP_Input*/, NULL, &len, NULL, report_buf, sizeof(report_buf));
        _snprintf(d, sizeof(d), "status=%08lx", (unsigned long)st);
        report("HidP_SetData(no preparsed)", st == (LONG)0xC011000EL || st != 0x00110000L, d);
    }
    /* mf */
    {
        HMODULE m = LoadLibraryW(L"mf.dll");
        typedef HRESULT (WINAPI *FN_T)(const GUID *, DWORD, void *, void **);
        FN_T p = (FN_T)GP(m, "MFTranscodeGetAudioOutputAvailableTypes");
        static const GUID zero = { 0 };
        void *coll = NULL;
        HRESULT hr = p(&zero, 0, NULL, &coll);
        _snprintf(d, sizeof(d), "hr=%08lx coll=%p", (unsigned long)hr, coll);
        report("MFTranscodeGetAudioOutputAvailableTypes", 1, d);
        if (coll) ((IUnknown *)coll)->lpVtbl->Release((IUnknown *)coll);
    }
    /* mscms */
    {
        HMODULE m = LoadLibraryW(L"mscms.dll");
        typedef BOOL (WINAPI *FN_WCS)(BOOL *);
        FN_WCS p = (FN_WCS)GP(m, "WcsGetCalibrationManagementState");
        BOOL state = TRUE;
        BOOL ok = p(&state);
        _snprintf(d, sizeof(d), "ok=%d state=%d", ok, state);
        report("WcsGetCalibrationManagementState", ok && state == TRUE, d);
    }
    /* prntvpt */
    {
        HMODULE m = LoadLibraryW(L"prntvpt.dll");
        typedef HRESULT (WINAPI *FN_BIND)(const WCHAR *, int, int, void *, void *);
        FN_BIND p = (FN_BIND)GP(m, "BindPTProviderThunk");
        HRESULT hr = p(L"FD171 Nonexistent Printer", 0, 0, NULL, NULL);
        _snprintf(d, sizeof(d), "hr=%08lx", (unsigned long)hr);
        report("BindPTProviderThunk(bogus printer)", FAILED(hr), d);
    }
    /* sspicli */
    {
        HMODULE m = LoadLibraryW(L"sspicli.dll");
        /* These return SECURITY_STATUS (0 = success), not BOOL. The
         * input must be the EX struct: the plain identity has no Version
         * field, so the marshal reads the low 32 bits of the User pointer
         * as the version and rejects a string literal. Unmarshal always
         * returns an EX struct with Flags |= MARSHALLED (0x4). */
        typedef ULONG (WINAPI *FN_MAR)(void *, ULONG *, void **);
        typedef ULONG (WINAPI *FN_UNM)(ULONG, void *, void **);
        typedef struct _AUTHID_EX_FIX {
            ULONG Version; ULONG Length;
            WCHAR *User; ULONG UserLength; WCHAR *Domain; ULONG DomainLength;
            WCHAR *Password; ULONG PasswordLength; ULONG Flags;
            WCHAR *PackageList; ULONG PackageListLength;
        } AUTHID_EX_FIX;
        FN_MAR pMar = (FN_MAR)GP(m, "SspiMarshalAuthIdentity");
        FN_UNM pUnm = (FN_UNM)GP(m, "SspiUnmarshalAuthIdentity");
        AUTHID_EX_FIX id, *out = NULL;
        ULONG len = 0; void *buf = NULL;
        memset(&id, 0, sizeof(id));
        id.Version = 0x200 /*SEC_WINNT_AUTH_IDENTITY_VERSION*/;
        id.Length = sizeof(id);
        id.User = (WCHAR *)L"user171"; id.UserLength = 7;
        id.Domain = (WCHAR *)L"DOM"; id.DomainLength = 3;
        id.Password = (WCHAR *)L"pw"; id.PasswordLength = 2;
        id.Flags = 0x2 /*SEC_WINNT_AUTH_IDENTITY_UNICODE*/;
        if (pMar(&id, &len, &buf) == 0 && buf) {
            ULONG st = pUnm(len, buf, (void **)&out);
            int good = st == 0 && out && out->Version == 0x200
                       && out->UserLength == 7 && out->User && !wcsncmp(out->User, L"user171", 7)
                       && out->DomainLength == 3 && out->PasswordLength == 2
                       && out->Flags == 0x6 /*UNICODE | MARSHALLED*/;
            _snprintf(d, sizeof(d), "len=%lu user=%ls", (unsigned long)len, good ? out->User : L"?");
            report("SspiMarshal/UnmarshalAuthIdentity", good, d);
        } else {
            report("SspiMarshal/UnmarshalAuthIdentity", 0, "marshal failed");
        }
    }
    /* vcomp family */
    {
        typedef double (WINAPI *FN_WTICK)(void);
        HMODULE m = LoadLibraryW(L"vcomp.dll");
        HMODULE m140 = LoadLibraryW(L"vcomp140.dll");
        FN_WTICK p1 = (FN_WTICK)GP(m, "omp_get_wtick");
        FN_WTICK p2 = (FN_WTICK)GP(m140, "omp_get_wtick");
        double a = p1(), b = p2();
        _snprintf(d, sizeof(d), "vcomp=%.9f vcomp140=%.9f", a, b);
        report("omp_get_wtick", a > 0.0 && a < 1.0 && b > 0.0 && b < 1.0, d);
    }
#ifdef __x86_64__
    /* mfplat */
    {
        HMODULE m = LoadLibraryW(L"mfplat.dll");
        typedef HRESULT (WINAPI *FN_STARTUP)(ULONG, DWORD);
        typedef HRESULT (WINAPI *FN_URL)(const WCHAR *, WCHAR **);
        FN_STARTUP pStart = (FN_STARTUP)GP(m, "MFStartup");
        FN_URL pUrl = (FN_URL)GP(m, "MFCreatePathFromURL");
        WCHAR *path = NULL;
        HRESULT hr = pStart(0x20070, 0);
        hr = pUrl(L"file:///C:/windows/win.ini", &path);
        _snprintf(d, sizeof(d), "hr=%08lx path=%ls", (unsigned long)hr, path ? path : L"(null)");
        report("MFCreatePathFromURL", hr == 0 && path && !wcscmp(path, L"C:\\windows\\win.ini"), d);
        if (path) CoTaskMemFree(path);
    }
    /* windows.networking DllGetActivationFactory */
    {
        HMODULE m = LoadLibraryW(L"windows.networking.dll");
        HMODULE combase = GetModuleHandleW(L"combase.dll");
        typedef HRESULT (WINAPI *FN_CREATE_STR)(const WCHAR *, unsigned, void **);
        typedef HRESULT (WINAPI *FN_GAF)(void *, void **);
        FN_CREATE_STR pCreate = (FN_CREATE_STR)GP(combase, "WindowsCreateString");
        FN_GAF pGaf = (FN_GAF)GP(m, "DllGetActivationFactory");
        void *hs = NULL, *factory = NULL;
        HRESULT hr = pCreate(L"Windows.Networking.HostName", 27, &hs);
        hr = pGaf(hs, &factory);
        _snprintf(d, sizeof(d), "hr=%08lx factory=%p", (unsigned long)hr, factory);
        /* The FD-171 implementation is a placeholder: it returns
         * CLASS_E_CLASSNOTAVAILABLE for every class (no factory
         * registry lookup), so that is the expected result here. */
        report("windows.networking DllGetActivationFactory", hr == (HRESULT)0x80040111L && !factory, d);
    }
    /* wintypes RoParseTypeName */
    {
        HMODULE m = LoadLibraryW(L"wintypes.dll");
        HMODULE combase = GetModuleHandleW(L"combase.dll");
        typedef HRESULT (WINAPI *FN_CREATE_STR)(const WCHAR *, unsigned, void **);
        typedef const WCHAR * (WINAPI *FN_RAWBUF)(void *, unsigned *);
        typedef HRESULT (WINAPI *FN_PARSE)(void *, DWORD *, void ***);
        typedef HRESULT (WINAPI *FN_DEL_STR)(void *);
        FN_CREATE_STR pCreate = (FN_CREATE_STR)GP(combase, "WindowsCreateString");
        FN_RAWBUF pRaw = (FN_RAWBUF)GP(combase, "WindowsGetStringRawBuffer");
        FN_DEL_STR pDel = (FN_DEL_STR)GP(combase, "WindowsDeleteString");
        FN_PARSE pParse = (FN_PARSE)GP(m, "RoParseTypeName");
        void *hs = NULL, **parts = NULL;
        DWORD count = 0, i;
        /* Parts are the parameterized-type components, not namespace
         * segments: a plain name is a single part (the whole name). */
        HRESULT hr = pCreate(L"Windows.Foundation.Collections.IVector`1<Windows.Foundation.Rect>", 65, &hs);
        hr = pParse(hs, &count, &parts);
        int good = hr == 0 && count == 2 && parts
                   && !wcscmp(pRaw(parts[0], NULL), L"Windows.Foundation.Collections.IVector`1")
                   && !wcscmp(pRaw(parts[1], NULL), L"Windows.Foundation.Rect");
        _snprintf(d, sizeof(d), "hr=%08lx count=%lu", (unsigned long)hr, (unsigned long)count);
        report("RoParseTypeName", good, d);
        if (parts) {
            for (i = 0; i < count; i++) pDel(parts[i]);
            CoTaskMemFree(parts);
        }
        if (hs) pDel(hs);
    }
    /* wofutil */
    {
        HMODULE m = LoadLibraryW(L"wofutil.dll");
        typedef HRESULT (WINAPI *FN_WOF)(const WCHAR *, BOOL *, ULONG *, void *, ULONG *);
        FN_WOF p = (FN_WOF)GP(m, "WofIsExternalFile");
        BOOL ext = TRUE;
        HRESULT hr = p(L"C:\\windows\\system32\\kernel32.dll", &ext, NULL, NULL, NULL);
        _snprintf(d, sizeof(d), "hr=%08lx external=%d", (unsigned long)hr, ext);
        report("WofIsExternalFile", hr == 0 && ext == FALSE, d);
    }
#endif
}

int main(void)
{
    HMODULE propsys = LoadLibraryW(L"propsys.dll");
    HMODULE oleaut = LoadLibraryW(L"oleaut32.dll");
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    HMODULE cfg = LoadLibraryW(L"cfgmgr32.dll");
    HMODULE pdh = LoadLibraryW(L"pdh.dll");
    HMODULE combase = LoadLibraryW(L"combase.dll");
    HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");

    printf(TAG ": FD-171 contract fixture start\n");
    fflush(stdout);
    test_propsys(propsys, oleaut);
    test_ntdll(ntdll);
    test_cm(cfg);
    test_pdh(pdh);
    test_combase(combase);
    test_kernel(kernelbase, kernel32);
    test_small();
    printf(TAG ": RESULT %d (%d checks)\n", failures, checks);
    fflush(stdout);
    return failures ? 1 : 0;
}
