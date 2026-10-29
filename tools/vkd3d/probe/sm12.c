/*
 * sm12.c — D3D12 shader-model capability probe (SM 6.0 caps lane,
 * 2026-10-26).
 *
 * Creates a device through the system-placed vkd3d-proton d3d12 pair and
 * asks it the runtime shader-model question via
 * ID3D12Device::CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL) — the same
 * query Godot's D3D12 driver makes in its shader-model sweep (it requires
 * 6.0+). Machine-readable verdict on stderr:
 *   "sm12: highest shader model = 0xNN"  (the value vkd3d reports)
 *   "sm12: RESULT 0" iff the reported model is >= D3D_SHADER_MODEL_6_0,
 *   "sm12: RESULT 1" otherwise (or on any earlier failure, with a
 *   "sm12: FAIL ..." reason line).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <stdio.h>

static void dbg(const char* s) { fprintf(stderr, "%s", s); fflush(stderr); }
#define FAIL(...) do { char b[256]; wsprintfA(b, __VA_ARGS__); dbg(b); \
    dbg("sm12: RESULT 1\n"); return 1; } while (0)

typedef HRESULT (WINAPI *PFN_D3D12_CREATE_DEVICE)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

int main(void)
{
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    if (!d3d12) FAIL("sm12: FAIL LoadLibrary(d3d12.dll) err=%lu\n", (unsigned long)GetLastError());
    PFN_D3D12_CREATE_DEVICE pD3D12CreateDevice =
        (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12, "D3D12CreateDevice");
    if (!pD3D12CreateDevice) FAIL("sm12: FAIL GetProcAddress(D3D12CreateDevice)\n");

    ID3D12Device* dev = NULL;
    HRESULT hr = pD3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
                                    &IID_ID3D12Device, (void**)&dev);
    if (FAILED(hr)) FAIL("sm12: FAIL D3D12CreateDevice hr=0x%08X\n", (unsigned)hr);
    dbg("sm12: D3D12CreateDevice OK\n");

    D3D12_FEATURE_DATA_SHADER_MODEL sm;
    sm.HighestShaderModel = (D3D_SHADER_MODEL)0x66; /* 6_6: ask down from the top (mingw header predates the constant) */
    hr = dev->lpVtbl->CheckFeatureSupport(dev, D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm));
    if (FAILED(hr)) FAIL("sm12: FAIL CheckFeatureSupport(SHADER_MODEL) hr=0x%08X\n", (unsigned)hr);
    { char b[160];
      wsprintfA(b, "sm12: highest shader model = 0x%02X (major %u minor %u)\n",
                (unsigned)sm.HighestShaderModel,
                ((unsigned)sm.HighestShaderModel >> 4) & 0xF,
                (unsigned)sm.HighestShaderModel & 0xF);
      dbg(b); }
    dev->lpVtbl->Release(dev);
    if (sm.HighestShaderModel >= D3D_SHADER_MODEL_6_0) { dbg("sm12: RESULT 0\n"); return 0; }
    dbg("sm12: RESULT 1\n");
    return 1;
}
