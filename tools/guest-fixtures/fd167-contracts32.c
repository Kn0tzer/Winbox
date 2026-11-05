/* fd167-contracts32.c -- FD-167 guest proof fixture (32-bit).
 *
 * Calls the demo-reachable Wine exports that the DLL export census found
 * to be winebuild stubs (raise 0x80000100 and kill the caller) and checks
 * each one answers with its documented contract instead:
 *
 *   bluetoothapis GATT x8   -> HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE)
 *   bthprops.cpl x3         -> FALSE (+ ERROR_INVALID_HANDLE for bad radio)
 *   winusb x8               -> FALSE + ERROR_INVALID_HANDLE
 *   wevtapi EvtRender       -> FALSE + ERROR_CALL_NOT_IMPLEMENTED
 *   mf MFCreateDeviceSource -> E_INVALIDARG / E_NOTIMPL, out = NULL
 *   mfplat MFCreateMediaBufferWrapper -> a working offset/length window
 *   propsys VariantCompare  -> -1/0/+1 ordering
 *
 * Against the stub runtime the process dies at the first stub call (the
 * RED state). Prints one line per check and a final RESULT line:
 * RESULT 0 means every check passed.
 */
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

static void report(const char *name, int ok, const char *detail)
{
    checks++;
    if (!ok) failures++;
    printf("fd167-32: %-46s %s %s\n", name, ok ? "ok  " : "FAIL", detail);
    fflush(stdout);
}

static void group(const char *name)
{
    printf("fd167-32: group %s\n", name);
    fflush(stdout);
}

#define HR_INVALID_HANDLE ((HRESULT)0x80070006L) /* HRESULT_FROM_WIN32(6) */

/* ---- bluetoothapis GATT ------------------------------------------------ */
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

static unsigned char chrc_buf[64], val_buf[64], desc_buf[64];
static void CALLBACK dummy_gatt_cb(int type, void *param, void *ctx) { (void)type; (void)param; (void)ctx; }

static void test_bluetooth(HMODULE bt, HMODULE props)
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
    report("bluetoothapis exports resolve", pGetServices && pGetChars && pGetDescs && pGetValue &&
           pSetValue && pSetDesc && pRegEvent && pUnregEvent, "");

    hr = pGetServices((HANDLE)(ULONG_PTR)0x1234, 0, NULL, &needed, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTGetServices bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pGetChars((HANDLE)(ULONG_PTR)0x1234, NULL, 0, NULL, &needed, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTGetCharacteristics bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pGetDescs((HANDLE)(ULONG_PTR)0x1234, NULL, 0, NULL, &needed, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTGetDescriptors bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pGetValue((HANDLE)(ULONG_PTR)0x1234, chrc_buf, 0, NULL, &needed, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTGetCharacteristicValue bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pSetValue((HANDLE)(ULONG_PTR)0x1234, chrc_buf, val_buf, 0, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTSetCharacteristicValue bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pSetDesc((HANDLE)(ULONG_PTR)0x1234, desc_buf, val_buf, 0, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTSetDescriptorValue bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pRegEvent((HANDLE)(ULONG_PTR)0x1234, 0, NULL, (void *)dummy_gatt_cb, NULL, &evt);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTRegisterEvent bad handle", hr == HR_INVALID_HANDLE, d);
    hr = pUnregEvent((HANDLE)(ULONG_PTR)0x1234, 0);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("BluetoothGATTUnregisterEvent bad handle", hr == HR_INVALID_HANDLE, d);

    group("bthprops.cpl");
    BTH_ENABLE pEnableDiscovery = (BTH_ENABLE)(void *)GetProcAddress(props, "BluetoothEnableDiscovery");
    BTH_ENABLE pEnableIncoming = (BTH_ENABLE)(void *)GetProcAddress(props, "BluetoothEnableIncomingConnections");
    BTH_QUERY pIsConnectable = (BTH_QUERY)(void *)GetProcAddress(props, "BluetoothIsConnectable");
    report("bthprops.cpl exports resolve", pEnableDiscovery && pEnableIncoming && pIsConnectable, "");

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
typedef BOOL (WINAPI *WUSB_CTRL)(void *, BYTE setup[8], BYTE *, ULONG, ULONG *, OVERLAPPED *);
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
    BYTE setup[8] = {0};
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

/* ---- wevtapi ------------------------------------------------------------ */
typedef BOOL (WINAPI *EVT_RENDER)(void *, void *, DWORD, DWORD, void *, DWORD *, DWORD *);

static void test_wevtapi(HMODULE we)
{
    char d[96];
    DWORD used = 0, count = 0;
    BOOL ok;
    DWORD gle;

    group("wevtapi");
    EVT_RENDER pRender = (EVT_RENDER)(void *)GetProcAddress(we, "EvtRender");
    report("wevtapi EvtRender resolves", pRender != NULL, "");
    SetLastError(0);
    ok = pRender(NULL, NULL, 0 /*EvtRenderEventValues*/, 0, NULL, &used, &count);
    gle = GetLastError();
    sprintf(d, "ret=%d gle=%lu", ok, (unsigned long)gle);
    report("EvtRender no fragment", ok == FALSE && gle == ERROR_CALL_NOT_IMPLEMENTED, d);
}

/* ---- mf / mfplat --------------------------------------------------------- */
typedef HRESULT (WINAPI *MF_DEVSRC)(void *, void **);
typedef HRESULT (WINAPI *MF_CREATE_ATTRS)(void **, UINT32);
typedef HRESULT (WINAPI *MF_CREATE_MEMBUF)(DWORD, void **);
typedef HRESULT (WINAPI *MF_WRAP)(void *, DWORD, DWORD, void **);

struct MFBufVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, const IID *, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(void *);
    ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *Lock)(void *, BYTE **, DWORD *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *Unlock)(void *);
    HRESULT (STDMETHODCALLTYPE *GetCurrentLength)(void *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *SetCurrentLength)(void *, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetMaxLength)(void *, DWORD *);
};
struct MFBuf { struct MFBufVtbl *lpVtbl; };

static void test_mf(HMODULE mf, HMODULE mfplat)
{
    char d[96];
    void *src = (void *)(ULONG_PTR)0xdead;
    void *attrs = NULL;
    struct MFBuf *inner = NULL, *wrap = NULL;
    HRESULT hr;
    DWORD v = 0;
    BYTE *p1 = NULL, *p2 = NULL;

    group("mf");
    MF_DEVSRC pDevSrc = (MF_DEVSRC)(void *)GetProcAddress(mf, "MFCreateDeviceSource");
    MF_CREATE_ATTRS pCreateAttrs = (MF_CREATE_ATTRS)(void *)GetProcAddress(mfplat, "MFCreateAttributes");
    MF_CREATE_MEMBUF pCreateBuf = (MF_CREATE_MEMBUF)(void *)GetProcAddress(mfplat, "MFCreateMemoryBuffer");
    MF_WRAP pWrap = (MF_WRAP)(void *)GetProcAddress(mfplat, "MFCreateMediaBufferWrapper");
    report("mf/mfplat exports resolve", pDevSrc && pCreateAttrs && pCreateBuf && pWrap, "");

    hr = pDevSrc(NULL, &src);
    sprintf(d, "hr=%08lx", (unsigned long)hr);
    report("MFCreateDeviceSource NULL attrs", hr == E_INVALIDARG, d);
    hr = pCreateAttrs(&attrs, 1);
    sprintf(d, "hr=%08lx attrs=%p", (unsigned long)hr, attrs);
    report("MFCreateAttributes works", hr == S_OK && attrs != NULL, d);
    hr = pDevSrc(attrs, &src);
    sprintf(d, "hr=%08lx src=%p", (unsigned long)hr, src);
    report("MFCreateDeviceSource real attrs", hr == E_NOTIMPL && src == NULL, d);

    group("mfplat buffer wrapper");
    hr = pCreateBuf(64, (void **)&inner);
    sprintf(d, "hr=%08lx inner=%p", (unsigned long)hr, (void *)inner);
    report("MFCreateMemoryBuffer(64)", hr == S_OK && inner != NULL, d);
    if (inner) {
        hr = pWrap(inner, 16, 32, (void **)&wrap);
        sprintf(d, "hr=%08lx wrap=%p", (unsigned long)hr, (void *)wrap);
        report("MFCreateMediaBufferWrapper(inner,16,32)", hr == S_OK && wrap != NULL, d);
        hr = pWrap(NULL, 0, 0, (void **)&wrap);
        sprintf(d, "hr=%08lx", (unsigned long)hr);
        report("MFCreateMediaBufferWrapper NULL inner", hr == E_INVALIDARG, d);
        hr = pWrap(inner, 100, 1, (void **)&wrap);
        sprintf(d, "hr=%08lx", (unsigned long)hr);
        report("MFCreateMediaBufferWrapper offset beyond max", hr == E_INVALIDARG, d);
        hr = pWrap(inner, 60, 8, (void **)&wrap);
        sprintf(d, "hr=%08lx", (unsigned long)hr);
        report("MFCreateMediaBufferWrapper window beyond max", hr == E_INVALIDARG, d);
    }
    if (wrap) {
        hr = wrap->lpVtbl->GetMaxLength(wrap, &v);
        sprintf(d, "hr=%08lx max=%lu", (unsigned long)hr, (unsigned long)v);
        report("wrapper GetMaxLength == 32", hr == S_OK && v == 32, d);
        hr = inner->lpVtbl->Lock(inner, &p1, NULL, NULL);
        hr = wrap->lpVtbl->Lock(wrap, &p2, NULL, NULL);
        sprintf(d, "inner=%p wrap=%p delta=%ld", p1, p2, (long)(p2 - p1));
        report("wrapper Lock == inner base + 16", p1 && p2 == p1 + 16, d);
        if (p2) {
            memset(p2, 0xAB, 32);
            hr = wrap->lpVtbl->Unlock(wrap);
        }
        if (p1) inner->lpVtbl->Unlock(inner);
        sprintf(d, "byte=%02x", p1 ? p1[16] : 0);
        report("wrapper writes land at inner+16", p1 && p1[16] == 0xAB, d);
        hr = wrap->lpVtbl->SetCurrentLength(wrap, 33);
        sprintf(d, "hr=%08lx", (unsigned long)hr);
        report("wrapper SetCurrentLength beyond window fails", FAILED(hr), d);
        hr = wrap->lpVtbl->SetCurrentLength(wrap, 20);
        hr = wrap->lpVtbl->GetCurrentLength(wrap, &v);
        sprintf(d, "hr=%08lx cur=%lu", (unsigned long)hr, (unsigned long)v);
        report("wrapper Set/GetCurrentLength 20", hr == S_OK && v == 20, d);
        wrap->lpVtbl->Release(wrap);
    }
    if (inner) inner->lpVtbl->Release(inner);
}

/* ---- propsys VariantCompare ---------------------------------------------- */
typedef int (WINAPI *VARIANT_COMPARE)(const VARIANT *, const VARIANT *);

static void test_propsys(HMODULE ps)
{
    char d[96];
    VARIANT a, b;
    int r;

    group("propsys");
    VARIANT_COMPARE pCompare = (VARIANT_COMPARE)(void *)GetProcAddress(ps, "VariantCompare");
    report("propsys VariantCompare resolves", pCompare != NULL, "");

    VariantInit(&a); VariantInit(&b);
    V_VT(&a) = VT_I4; V_I4(&a) = 5; V_VT(&b) = VT_I4; V_I4(&b) = 7;
    r = pCompare(&a, &b);
    sprintf(d, "r=%d", r);
    report("VariantCompare I4 5 vs 7", r == -1, d);
    r = pCompare(&b, &a);
    sprintf(d, "r=%d", r);
    report("VariantCompare I4 7 vs 5", r == 1, d);
    r = pCompare(&a, &a);
    sprintf(d, "r=%d", r);
    report("VariantCompare I4 5 vs 5", r == 0, d);

    V_VT(&a) = VT_BSTR; V_BSTR(&a) = SysAllocString(L"abc");
    V_VT(&b) = VT_BSTR; V_BSTR(&b) = SysAllocString(L"abd");
    r = pCompare(&a, &b);
    sprintf(d, "r=%d", r);
    report("VariantCompare BSTR abc vs abd", r == -1, d);
    VariantClear(&a); VariantClear(&b);

    V_VT(&a) = VT_LPWSTR; V_BSTR(&a) = (BSTR)L"same";
    V_VT(&b) = VT_LPWSTR; V_BSTR(&b) = (BSTR)L"same";
    r = pCompare(&a, &b);
    sprintf(d, "r=%d (propvariant LPWSTR)", r);
    report("VariantCompare LPWSTR same vs same", r == 0, d);

    VariantInit(&a); VariantInit(&b);
    r = pCompare(&a, &b);
    sprintf(d, "r=%d", r);
    report("VariantCompare EMPTY vs EMPTY", r == 0, d);
    V_VT(&b) = VT_I4; V_I4(&b) = 0;
    r = pCompare(&a, &b);
    sprintf(d, "r=%d", r);
    report("VariantCompare EMPTY vs I4 0", r == -1, d);

    V_VT(&a) = VT_BOOL; V_BOOL(&a) = VARIANT_FALSE;
    V_VT(&b) = VT_BOOL; V_BOOL(&b) = VARIANT_TRUE;
    r = pCompare(&a, &b);
    sprintf(d, "r=%d", r);
    report("VariantCompare BOOL false vs true", r == -1, d);
    r = pCompare(&b, &a);
    sprintf(d, "r=%d", r);
    report("VariantCompare BOOL true vs false", r == 1, d);
    r = pCompare(&b, &b);
    sprintf(d, "r=%d", r);
    report("VariantCompare BOOL true vs true", r == 0, d);

    V_VT(&a) = VT_DATE; V_DATE(&a) = 1.5;
    V_VT(&b) = VT_DATE; V_DATE(&b) = 2.5;
    r = pCompare(&a, &b);
    sprintf(d, "r=%d", r);
    report("VariantCompare DATE 1.5 vs 2.5", r == -1, d);
}

int main(void)
{
    HMODULE bt = LoadLibraryA("bluetoothapis.dll");
    HMODULE props = LoadLibraryA("bthprops.cpl");
    HMODULE wu = LoadLibraryA("winusb.dll");
    HMODULE we = LoadLibraryA("wevtapi.dll");
    HMODULE mf = LoadLibraryA("mf.dll");
    HMODULE mfplat = LoadLibraryA("mfplat.dll");
    HMODULE ps = LoadLibraryA("propsys.dll");
    char d[96];
    sprintf(d, "bt=%p props=%p wu=%p we=%p mf=%p mfplat=%p ps=%p", bt, props, wu, we, mf, mfplat, ps);
    report("all target DLLs load", bt && props && wu && we && mf && mfplat && ps, d);

    if (bt && props) test_bluetooth(bt, props);
    if (wu) test_winusb(wu);
    if (we) test_wevtapi(we);
    if (mf && mfplat) test_mf(mf, mfplat);
    if (ps) test_propsys(ps);

    printf("fd167-32: RESULT %d (%d checks)\n", failures, checks);
    fflush(stdout);
    return failures ? 1 : 0;
}
