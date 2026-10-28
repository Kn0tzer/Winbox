/*
 * res12.c -- D3D12 committed-resource wall probe (committed-resource lane).
 *
 * No shaders, no pipelines, no draws: just D3D12CreateDevice plus the
 * CreateCommittedResource matrix, so the wall is characterized without
 * any shader-compiler or descriptor-addressing involvement:
 *   default-buffer-64k, default-buffer-1m, upload-buffer-64k (+Map round-trip),
 *   readback-buffer-1k, readback-buffer-4m (+Map round-trip),
 *   default-tex-rt (4x4 RGBA8 ALLOW_RENDER_TARGET),
 *   default-tex-uav (256x256 RGBA8 ALLOW_UNORDERED_ACCESS),
 *   custom-writeback-buffer, custom-writecombine-buffer, gpu-upload-buffer.
 * Per-case verdict lines are prefixed "res12: CASE <name>"; the final
 * "res12: RESULT 0" requires every case to succeed (Map round-trips
 * included). A failing case prints its HRESULT; the pre-fix expectation
 * (from the Route A report: root12 CBV stops at the RT readback resource
 * with hr=0x8007000E, uav12 EXEC cases stop at their readback buffer)
 * is that the READBACK and CUSTOM-WRITEBACK cases fail with
 * E_OUTOFMEMORY while the rest pass.
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o res12.exe res12.c -lole32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <stdio.h>
#include <string.h>

static void dbg(const char* s) { fprintf(stderr, "%s", s); fflush(stderr); }

typedef HRESULT (WINAPI *PFN_D3D12_CREATE_DEVICE)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

static ID3D12Device* g_dev;
static int g_fail;

static ID3D12Resource* make_res(const D3D12_HEAP_PROPERTIES* heap, const D3D12_RESOURCE_DESC* rd,
                                D3D12_RESOURCE_STATES initial, const char* name, HRESULT* outHr)
{
    ID3D12Resource* res = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(g_dev, heap, D3D12_HEAP_FLAG_NONE, rd,
                                                      initial, NULL, &IID_ID3D12Resource, (void**)&res);
    char b[256];
    if (outHr) *outHr = hr;
    if (FAILED(hr)) {
        wsprintfA(b, "res12: CASE %-26s FAIL hr=0x%08X\n", name, (unsigned)hr);
        dbg(b); g_fail = 1; return NULL;
    }
    wsprintfA(b, "res12: CASE %-26s OK\n", name); dbg(b);
    return res;
}

static D3D12_RESOURCE_DESC buf_desc(UINT64 size, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC rd = {0};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;
    return rd;
}

static D3D12_RESOURCE_DESC tex_desc(UINT w, UINT h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC rd = {0};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = h;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = fmt;
    rd.SampleDesc.Count = 1; rd.Flags = flags;
    return rd;
}

static int map_roundtrip(ID3D12Resource* res, UINT64 size, const char* name)
{
    void* m = NULL; D3D12_RANGE rr = {0, 0};
    HRESULT hr = ID3D12Resource_Map(res, 0, &rr, &m);
    char b[256];
    if (FAILED(hr)) { wsprintfA(b, "res12: CASE %-26s FAIL (Map hr=0x%08X)\n", name, (unsigned)hr); dbg(b); g_fail = 1; return 0; }
    unsigned char* p = (unsigned char*)m;
    for (UINT64 i = 0; i < size; i++) p[i] = (unsigned char)(i * 7 + 3);
    ID3D12Resource_Unmap(res, 0, NULL);
    D3D12_RANGE rr2 = {0, (SIZE_T)size};
    hr = ID3D12Resource_Map(res, 0, &rr2, &m);
    if (FAILED(hr)) { wsprintfA(b, "res12: CASE %-26s FAIL (Map2 hr=0x%08X)\n", name, (unsigned)hr); dbg(b); g_fail = 1; return 0; }
    p = (unsigned char*)m;
    int bad = 0;
    for (UINT64 i = 0; i < size; i++) if (p[i] != (unsigned char)(i * 7 + 3)) bad++;
    ID3D12Resource_Unmap(res, 0, NULL);
    if (bad) { wsprintfA(b, "res12: CASE %-26s FAIL (round-trip bad=%d)\n", name, bad); dbg(b); g_fail = 1; return 0; }
    wsprintfA(b, "res12: CASE %-26s OK (Map round-trip)\n", name); dbg(b);
    return 1;
}

#define HEAP_T(t) ({ D3D12_HEAP_PROPERTIES _h = {0}; _h.Type = (t); _h; })

int main(void)
{
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    if (!d3d12) { dbg("res12: FAIL LoadLibrary(d3d12.dll)\n"); dbg("res12: RESULT 1\n"); return 1; }
    PFN_D3D12_CREATE_DEVICE pCreate =
        (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12, "D3D12CreateDevice");
    if (!pCreate) { dbg("res12: FAIL GetProcAddress(D3D12CreateDevice)\n"); dbg("res12: RESULT 1\n"); return 1; }
    HRESULT hr = pCreate(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&g_dev);
    if (FAILED(hr)) { char b[128]; wsprintfA(b, "res12: FAIL D3D12CreateDevice hr=0x%08X\n", (unsigned)hr); dbg(b); dbg("res12: RESULT 1\n"); return 1; }
    dbg("res12: D3D12CreateDevice OK\n");

    ID3D12Resource* r;
    D3D12_HEAP_PROPERTIES h;
    D3D12_RESOURCE_DESC d;

    h = HEAP_T(D3D12_HEAP_TYPE_DEFAULT);
    d = buf_desc(65536, 0);
    r = make_res(&h, &d, D3D12_RESOURCE_STATE_COPY_DEST, "default-buffer-64k", NULL);
    if (r) ID3D12Resource_Release(r);

    h = HEAP_T(D3D12_HEAP_TYPE_DEFAULT);
    d = buf_desc(1024 * 1024, 0);
    r = make_res(&h, &d, D3D12_RESOURCE_STATE_COPY_DEST, "default-buffer-1m", NULL);
    if (r) ID3D12Resource_Release(r);

    h = HEAP_T(D3D12_HEAP_TYPE_UPLOAD);
    d = buf_desc(65536, 0);
    r = make_res(&h, &d, D3D12_RESOURCE_STATE_GENERIC_READ, "upload-buffer-64k", NULL);
    if (r) { map_roundtrip(r, 65536, "upload-buffer-64k-map"); ID3D12Resource_Release(r); }

    h = HEAP_T(D3D12_HEAP_TYPE_READBACK);
    d = buf_desc(1024, 0);
    r = make_res(&h, &d, D3D12_RESOURCE_STATE_COPY_DEST, "readback-buffer-1k", NULL);
    if (r) { map_roundtrip(r, 1024, "readback-buffer-1k-map"); ID3D12Resource_Release(r); }

    h = HEAP_T(D3D12_HEAP_TYPE_READBACK);
    d = buf_desc(4 * 1024 * 1024, 0);
    r = make_res(&h, &d, D3D12_RESOURCE_STATE_COPY_DEST, "readback-buffer-4m", NULL);
    if (r) { map_roundtrip(r, 4 * 1024 * 1024, "readback-buffer-4m-map"); ID3D12Resource_Release(r); }

    h = HEAP_T(D3D12_HEAP_TYPE_DEFAULT);
    d = tex_desc(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    r = make_res(&h, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, "default-tex-rt", NULL);
    if (r) ID3D12Resource_Release(r);

    h = HEAP_T(D3D12_HEAP_TYPE_DEFAULT);
    d = tex_desc(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    r = make_res(&h, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "default-tex-uav", NULL);
    if (r) ID3D12Resource_Release(r);

    memset(&h, 0, sizeof(h));
    h.Type = D3D12_HEAP_TYPE_CUSTOM;
    h.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    h.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    d = buf_desc(65536, 0);
    r = make_res(&h, &d, D3D12_RESOURCE_STATE_COPY_DEST, "custom-writeback-buffer", NULL);
    if (r) ID3D12Resource_Release(r);

    memset(&h, 0, sizeof(h));
    h.Type = D3D12_HEAP_TYPE_CUSTOM;
    h.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
    h.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    d = buf_desc(65536, 0);
    r = make_res(&h, &d, D3D12_RESOURCE_STATE_GENERIC_READ, "custom-writecombine-buffer", NULL);
    if (r) ID3D12Resource_Release(r);

    /* MinGW header predates GPU_UPLOAD (value 5 in newer Agility SDKs). */
    h = HEAP_T((D3D12_HEAP_TYPE)5);
    d = buf_desc(65536, 0);
    r = make_res(&h, &d, D3D12_RESOURCE_STATE_GENERIC_READ, "gpu-upload-buffer", NULL);
    if (r) ID3D12Resource_Release(r);

    ID3D12Device_Release(g_dev);
    if (!g_fail) { dbg("res12: RESULT 0\n"); return 0; }
    dbg("res12: RESULT 1\n"); return 1;
}
