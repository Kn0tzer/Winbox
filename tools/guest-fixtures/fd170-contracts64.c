/* fd170-contracts64.c -- FD-170 guest proof fixture (64-bit).
 *
 * FD-170 covers whole shipped DLLs that are >=90% winebuild stubs. Three
 * of them gain real behaviour on the x86-64 side in this lane, and this
 * fixture proves the behaviour a guest can actually trigger:
 *
 *   bluetoothapis.dll -- Wine 10.0's own radio/SDP code backported into
 *     the Wine 8.0 pipeline, plus the FD-167 GATT contract bodies (the
 *     documented no-radio validation, in the native order). Checks cover
 *     both layers: the 10.0 radio enumeration answers (invalid-parameter
 *     / revision-mismatch / no-more-items on a radio-less machine,
 *     invalid-handle on a bad find-close) and the GATT contracts
 *     (bad handle -> HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE), with the
 *     E_POINTER / E_INVALIDARG validation order intact).
 *   winusb.dll -- the FD-167 no-device contract bodies (FALSE +
 *     ERROR_INVALID_HANDLE; WinUsb_Initialize validates its
 *     out-parameter first with ERROR_INVALID_PARAMETER).
 *   netutils.dll -- Wine 10.0's NetApiBuffer* implementations: real
 *     process-heap buffer management (allocate / size / reallocate with
 *     contents preserved / free), not error answers.
 *
 * Against the pre-change runtime every DLL here is spec stubs: the
 * first call raises the Wine stub exception 0x80000100 and kills the
 * process, so no RESULT line ever prints (the RED state). Prints one
 * line per check and a final RESULT line: RESULT 0 means every check
 * passed.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

static void report(const char *name, int ok, const char *detail)
{
    checks++;
    if (!ok) failures++;
    printf("fd170-64: %-52s %s %s\n", name, ok ? "ok  " : "FAIL", detail);
    fflush(stdout);
}

static void group(const char *name)
{
    printf("fd170-64: group %s\n", name);
    fflush(stdout);
}

#define HR_INVALID_HANDLE ((HRESULT)0x80070006L) /* HRESULT_FROM_WIN32(6) */
#define HR_POINTER        ((HRESULT)0x80004003L) /* E_POINTER */
#define HR_INVALIDARG     ((HRESULT)0x80070057L) /* E_INVALIDARG */

/* ---- bluetoothapis ----------------------------------------------------- */
typedef HRESULT (WINAPI *GATT_GET_SERVICES)(HANDLE, USHORT, void *, USHORT *, ULONG);
typedef HRESULT (WINAPI *GATT_GET_CHARS)(HANDLE, void *, USHORT, void *, USHORT *, ULONG);
typedef HRESULT (WINAPI *GATT_GET_DESCS)(HANDLE, void *, USHORT, void *, USHORT *, ULONG);
typedef HRESULT (WINAPI *GATT_GET_VALUE)(HANDLE, void *, ULONG, void *, USHORT *, ULONG);
typedef HRESULT (WINAPI *GATT_SET_VALUE)(HANDLE, void *, void *, ULONGLONG, ULONG);
typedef HRESULT (WINAPI *GATT_SET_DESC)(HANDLE, void *, void *, ULONGLONG, ULONG);
typedef HRESULT (WINAPI *GATT_REG_EVENT)(HANDLE, int, void *, void *, void *, HANDLE *);
typedef HRESULT (WINAPI *GATT_UNREG_EVENT)(HANDLE, ULONG);
typedef BOOL (WINAPI *BTH_ENABLE)(HANDLE, BOOL);
typedef BOOL (WINAPI *BTH_QUERY)(HANDLE);
typedef void *(WINAPI *BTH_FIND_FIRST_RADIO)(void *, HANDLE *);
typedef BOOL (WINAPI *BTH_FIND_RADIO_CLOSE)(void *);

struct find_radio_params { DWORD dwSize; };

static unsigned char chrc_buf[64], val_buf[64], desc_buf[64];
static void CALLBACK dummy_gatt_cb(int type, void *param, void *ctx) { (void)type; (void)param; (void)ctx; }

static void test_bluetooth(HMODULE bt)
{
    char d[96];
    USHORT needed = 0;
    HANDLE evt = NULL;
    HRESULT hr;
    BOOL ok;
    DWORD gle;

    group("bluetoothapis GATT");
    GATT_GET_SERVICES pGetServices = (GATT_GET_SERVICES)(void *)GetProcAddress(bt, "BluetoothGATTGetServices");
    GATT_GET_CHARS pGetChars = (GATT_GET_CHARS)(void *)GetProcAddress(bt, "BluetoothGATTGetCharacteristics");
    GATT_GET_DESCS pGetDescs = (GATT_GET_DESCS)(void *)GetProcAddress(bt, "BluetoothGATTGetDescriptors");
    GATT_GET_VALUE pGetValue = (GATT_GET_VALUE)(void *)GetProcAddress(bt, "BluetoothGATTGetCharacteristicValue");
    GATT_SET_VALUE pSetValue = (GATT_SET_VALUE)(void *)GetProcAddress(bt, "BluetoothGATTSetCharacteristicValue");
    GATT_SET_DESC pSetDesc = (GATT_SET_DESC)(void *)GetProcAddress(bt, "BluetoothGATTSetDescriptorValue");
    GATT_REG_EVENT pRegEvent = (GATT_REG_EVENT)(void *)GetProcAddress(bt, "BluetoothGATTRegisterEvent");
    GATT_UNREG_EVENT pUnregEvent = (GATT_UNREG_EVENT)(void *)GetProcAddress(bt, "BluetoothGATTUnregisterEvent");
    report("bluetoothapis GATT exports resolve", pGetServices && pGetChars && pGetDescs && pGetValue &&
           pSetValue && pSetDesc && pRegEvent && pUnregEvent, "");

    hr = pGetServices((HANDLE)(ULONG_PTR)0x1234, 0, NULL, &needed, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTGetServices bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pGetServices((HANDLE)(ULONG_PTR)0x1234, 0, NULL, NULL, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTGetServices NULL actual -> E_POINTER", hr == HR_POINTER, d);
    hr = pGetServices((HANDLE)(ULONG_PTR)0x1234, 1, NULL, &needed, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTGetServices count/no buffer -> E_INVALIDARG", hr == HR_INVALIDARG, d);
    hr = pGetChars((HANDLE)(ULONG_PTR)0x1234, NULL, 0, NULL, &needed, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTGetCharacteristics bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pGetDescs((HANDLE)(ULONG_PTR)0x1234, NULL, 0, NULL, &needed, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTGetDescriptors bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pGetValue((HANDLE)(ULONG_PTR)0x1234, chrc_buf, 0, NULL, &needed, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTGetCharacteristicValue bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pGetValue(NULL, chrc_buf, 0, NULL, &needed, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTGetCharacteristicValue NULL device", hr == HR_INVALID_HANDLE, d);
    hr = pSetValue((HANDLE)(ULONG_PTR)0x1234, chrc_buf, val_buf, 0, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTSetCharacteristicValue bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pSetDesc((HANDLE)(ULONG_PTR)0x1234, desc_buf, val_buf, 0, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTSetDescriptorValue bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pRegEvent((HANDLE)(ULONG_PTR)0x1234, 0, NULL, (void *)dummy_gatt_cb, NULL, &evt);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTRegisterEvent bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pRegEvent((HANDLE)(ULONG_PTR)0x1234, 0, NULL, (void *)dummy_gatt_cb, NULL, NULL);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTRegisterEvent NULL out -> E_POINTER", hr == HR_POINTER, d);
    hr = pRegEvent((HANDLE)(ULONG_PTR)0x1234, 0, NULL, NULL, NULL, &evt);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTRegisterEvent NULL callback -> E_INVALIDARG", hr == HR_INVALIDARG, d);
    hr = pRegEvent(NULL, 0, NULL, (void *)dummy_gatt_cb, NULL, &evt);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTRegisterEvent NULL device", hr == HR_INVALID_HANDLE, d);
    hr = pUnregEvent((HANDLE)(ULONG_PTR)0x1234, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTUnregisterEvent bad handle", hr == HR_INVALID_HANDLE, d);

    group("bluetoothapis radio (Wine 10.0 code)");
    BTH_FIND_FIRST_RADIO pFindFirstRadio = (BTH_FIND_FIRST_RADIO)(void *)GetProcAddress(bt, "BluetoothFindFirstRadio");
    BTH_FIND_RADIO_CLOSE pFindRadioClose = (BTH_FIND_RADIO_CLOSE)(void *)GetProcAddress(bt, "BluetoothFindRadioClose");
    BTH_ENABLE pEnableDiscovery = (BTH_ENABLE)(void *)GetProcAddress(bt, "BluetoothEnableDiscovery");
    BTH_ENABLE pEnableIncoming = (BTH_ENABLE)(void *)GetProcAddress(bt, "BluetoothEnableIncomingConnections");
    BTH_QUERY pIsConnectable = (BTH_QUERY)(void *)GetProcAddress(bt, "BluetoothIsConnectable");
    report("bluetoothapis radio exports resolve", pFindFirstRadio && pFindRadioClose &&
           pEnableDiscovery && pEnableIncoming && pIsConnectable, "");

    {
        struct find_radio_params params;
        HANDLE radio = NULL;
        void *find;

        params.dwSize = sizeof(params);
        SetLastError(0);
        find = pFindFirstRadio(NULL, &radio);
        gle = GetLastError();
        sprintf(d, "find=%p gle=%lu", find, (unsigned long)gle);
        report("BluetoothFindFirstRadio NULL params", find == NULL && gle == ERROR_INVALID_PARAMETER, d);

        params.dwSize = sizeof(params) + 1;
        SetLastError(0);
        find = pFindFirstRadio(&params, &radio);
        gle = GetLastError();
        sprintf(d, "find=%p gle=%lu", find, (unsigned long)gle);
        report("BluetoothFindFirstRadio bad dwSize", find == NULL && gle == ERROR_REVISION_MISMATCH, d);

        params.dwSize = sizeof(params);
        SetLastError(0);
        find = pFindFirstRadio(&params, &radio);
        gle = GetLastError();
        sprintf(d, "find=%p gle=%lu", find, (unsigned long)gle);
        report("BluetoothFindFirstRadio no radio present", find == NULL && gle == ERROR_NO_MORE_ITEMS, d);

        SetLastError(0);
        ok = pFindRadioClose(NULL);
        gle = GetLastError();
        sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)gle);
        report("BluetoothFindRadioClose NULL", ok == FALSE && gle == ERROR_INVALID_HANDLE, d);
    }

    ok = pEnableDiscovery(NULL, TRUE);
    sprintf(d, "ret=%d", ok);
    report("BluetoothEnableDiscovery NULL radio", ok == FALSE, d);
    SetLastError(0);
    ok = pEnableDiscovery((HANDLE)(ULONG_PTR)0x1234, TRUE);
    gle = GetLastError();
    sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)gle);
    report("BluetoothEnableDiscovery bad radio", ok == FALSE && gle == ERROR_INVALID_HANDLE, d);
    ok = pEnableIncoming(NULL, TRUE);
    sprintf(d, "ret=%d", ok);
    report("BluetoothEnableIncomingConnections NULL radio", ok == FALSE, d);
    SetLastError(0);
    ok = pEnableIncoming((HANDLE)(ULONG_PTR)0x1234, TRUE);
    gle = GetLastError();
    sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)gle);
    report("BluetoothEnableIncomingConnections bad radio", ok == FALSE && gle == ERROR_INVALID_HANDLE, d);
    ok = pIsConnectable(NULL);
    sprintf(d, "ret=%d", ok);
    report("BluetoothIsConnectable NULL radio", ok == FALSE, d);
    ok = pIsConnectable((HANDLE)(ULONG_PTR)0x1234);
    sprintf(d, "ret=%d", ok);
    report("BluetoothIsConnectable bad radio", ok == FALSE, d);
}

/* ---- winusb ------------------------------------------------------------ */
typedef BOOL (WINAPI *WUSB_INIT)(HANDLE, void **);
typedef BOOL (WINAPI *WUSB_ASSOC)(void *, BYTE, void **);
typedef BOOL (WINAPI *WUSB_OVLRES)(void *, OVERLAPPED *, DWORD *, BOOL);
typedef struct { BYTE bytes[8]; } WUSB_SETUP;
typedef BOOL (WINAPI *WUSB_CTRL)(void *, WUSB_SETUP, BYTE *, ULONG, ULONG *, OVERLAPPED *);
typedef BOOL (WINAPI *WUSB_PIPE)(void *, BYTE, BYTE *, ULONG, ULONG *, OVERLAPPED *);
typedef BOOL (WINAPI *WUSB_RESET)(void *, BYTE);
typedef BOOL (WINAPI *WUSB_ALT)(void *, BYTE);

static void test_winusb(HMODULE wu)
{
    char d[96];
    void *iface = (void *)(ULONG_PTR)0xdead;
    BYTE buf[16];
    ULONG transferred = 0;
    OVERLAPPED ovl;
    WUSB_SETUP setup;
    BOOL ok;
    DWORD gle;

    group("winusb");
    WUSB_INIT pInit = (WUSB_INIT)(void *)GetProcAddress(wu, "WinUsb_Initialize");
    WUSB_ASSOC pAssoc = (WUSB_ASSOC)(void *)GetProcAddress(wu, "WinUsb_GetAssociatedInterface");
    WUSB_OVLRES pOvlRes = (WUSB_OVLRES)(void *)GetProcAddress(wu, "WinUsb_GetOverlappedResult");
    WUSB_CTRL pCtrl = (WUSB_CTRL)(void *)GetProcAddress(wu, "WinUsb_ControlTransfer");
    WUSB_PIPE pRead = (WUSB_PIPE)(void *)GetProcAddress(wu, "WinUsb_ReadPipe");
    WUSB_PIPE pWrite = (WUSB_PIPE)(void *)GetProcAddress(wu, "WinUsb_WritePipe");
    WUSB_RESET pReset = (WUSB_RESET)(void *)GetProcAddress(wu, "WinUsb_ResetPipe");
    WUSB_ALT pAlt = (WUSB_ALT)(void *)GetProcAddress(wu, "WinUsb_SetCurrentAlternateSetting");
    report("winusb exports resolve", pInit && pAssoc && pOvlRes && pCtrl && pRead && pWrite && pReset && pAlt, "");

    memset(&ovl, 0, sizeof(ovl));
    memset(setup.bytes, 0, sizeof(setup.bytes));
    SetLastError(0);
    ok = pInit((HANDLE)(ULONG_PTR)0x1234, &iface);
    gle = GetLastError();
    sprintf(d, "ret=%d gle=%lu iface=%p", ok, (unsigned long)gle, iface);
    report("WinUsb_Initialize bad device", ok == FALSE && gle == ERROR_INVALID_HANDLE && iface == NULL, d);
    SetLastError(0);
    ok = pInit(NULL, &iface);
    gle = GetLastError();
    sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)gle);
    report("WinUsb_Initialize NULL device", ok == FALSE && gle == ERROR_INVALID_HANDLE, d);
    SetLastError(0);
    ok = pInit((HANDLE)(ULONG_PTR)0x1234, NULL);
    gle = GetLastError();
    sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)gle);
    report("WinUsb_Initialize NULL out", ok == FALSE && gle == ERROR_INVALID_PARAMETER, d);
    SetLastError(0);
    ok = pAssoc((void *)(ULONG_PTR)0x1234, 0, NULL);
    gle = GetLastError();
    sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)gle);
    report("WinUsb_GetAssociatedInterface NULL out", ok == FALSE && gle == ERROR_INVALID_PARAMETER, d);

#define WUSB_BAD(name, call) do { \
    SetLastError(0); \
    ok = (call); \
    gle = GetLastError(); \
    sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)gle); \
    report(name, ok == FALSE && gle == ERROR_INVALID_HANDLE, d); \
} while (0)
    WUSB_BAD("WinUsb_GetAssociatedInterface bad iface", pAssoc((void *)(ULONG_PTR)0x1234, 0, &iface));
    WUSB_BAD("WinUsb_GetOverlappedResult bad iface", pOvlRes((void *)(ULONG_PTR)0x1234, &ovl, &transferred, TRUE));
    WUSB_BAD("WinUsb_ControlTransfer bad iface", pCtrl((void *)(ULONG_PTR)0x1234, setup, buf, sizeof(buf), &transferred, NULL));
    WUSB_BAD("WinUsb_ReadPipe bad iface", pRead((void *)(ULONG_PTR)0x1234, 0x81, buf, sizeof(buf), &transferred, NULL));
    WUSB_BAD("WinUsb_WritePipe bad iface", pWrite((void *)(ULONG_PTR)0x1234, 0x01, buf, sizeof(buf), &transferred, NULL));
    WUSB_BAD("WinUsb_ResetPipe bad iface", pReset((void *)(ULONG_PTR)0x1234, 0x81));
    WUSB_BAD("WinUsb_SetCurrentAlternateSetting bad iface", pAlt((void *)(ULONG_PTR)0x1234, 0));
#undef WUSB_BAD
}

/* ---- netutils ---------------------------------------------------------- */
typedef DWORD (WINAPI *NU_ALLOC)(DWORD, void **);
typedef DWORD (WINAPI *NU_FREE)(void *);
typedef DWORD (WINAPI *NU_REALLOC)(void *, DWORD, void **);
typedef DWORD (WINAPI *NU_SIZE)(void *, DWORD *);

static void test_netutils(HMODULE nu)
{
    char d[96];
    void *buf = NULL, *buf2 = NULL;
    DWORD st, sz = 0;
    int i, intact;

    group("netutils");
    NU_ALLOC pAlloc = (NU_ALLOC)(void *)GetProcAddress(nu, "NetApiBufferAllocate");
    NU_FREE pFree = (NU_FREE)(void *)GetProcAddress(nu, "NetApiBufferFree");
    NU_REALLOC pRealloc = (NU_REALLOC)(void *)GetProcAddress(nu, "NetApiBufferReallocate");
    NU_SIZE pSize = (NU_SIZE)(void *)GetProcAddress(nu, "NetApiBufferSize");
    report("netutils exports resolve", pAlloc && pFree && pRealloc && pSize, "");

    st = pAlloc(128, &buf);
    sprintf(d, "st=%lu buf=%p", (unsigned long)st, buf);
    report("NetApiBufferAllocate", st == 0 && buf != NULL, d);
    if (!buf) return;
    for (i = 0; i < 64; i++) ((BYTE *)buf)[i] = (BYTE)(0xa0 + i);

    st = pSize(buf, &sz);
    sprintf(d, "st=%lu size=%lu", (unsigned long)st, (unsigned long)sz);
    report("NetApiBufferSize", st == 0 && sz >= 128, d);

    st = pRealloc(buf, 512, &buf2);
    intact = buf2 != NULL;
    if (intact)
        for (i = 0; i < 64; i++)
            if (((BYTE *)buf2)[i] != (BYTE)(0xa0 + i)) intact = 0;
    sprintf(d, "st=%lu buf=%p intact=%d", (unsigned long)st, buf2, intact);
    report("NetApiBufferReallocate keeps contents", st == 0 && intact, d);
    if (!buf2) return;

    sz = 0;
    st = pSize(buf2, &sz);
    sprintf(d, "st=%lu size=%lu", (unsigned long)st, (unsigned long)sz);
    report("NetApiBufferSize after realloc", st == 0 && sz >= 512, d);

    st = pFree(buf2);
    sprintf(d, "st=%lu", (unsigned long)st);
    report("NetApiBufferFree", st == 0, d);

    st = pAlloc(16, NULL);
    sprintf(d, "st=%lu", (unsigned long)st);
    report("NetApiBufferAllocate NULL out", st == ERROR_INVALID_PARAMETER, d);
}

int main(void)
{
    HMODULE bt, wu, nu;

    printf("fd170-64: fixture start\n");
    fflush(stdout);
    bt = LoadLibraryA("bluetoothapis.dll");
    wu = LoadLibraryA("winusb.dll");
    nu = LoadLibraryA("netutils.dll");
    report("libraries load", bt && wu && nu, "");
    if (bt) test_bluetooth(bt);
    if (wu) test_winusb(wu);
    if (nu) test_netutils(nu);
    printf("fd170-64: checks=%d failures=%d\n", checks, failures);
    printf("fd170-64: RESULT %d\n", failures);
    fflush(stdout);
    return failures != 0;
}
