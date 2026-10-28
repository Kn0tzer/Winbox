/*
 * rb12.c -- staged readback experiment (readback-zeros lane, 2026-10-28).
 *
 * root12's CBV case and uav12's float EXEC cases complete without error
 * but read back all zeros. This probe separates the stages with ONE
 * variable changed per variant (same device/queue/fence, fresh resources
 * and command list per variant):
 *   BUF     : upload -> default -> readback BUFFER copies only (no image,
 *             no render pass). Tests buffer copies + fence + host mapping.
 *   CLEAR   : clear-only render target (magenta) -> copy -> readback.
 *             Tests clear + RT->readback image copy + fence + map.
 *   UPLDTEX : upload-filled texture -> copy -> RT -> copy -> readback.
 *             Tests buffer->image and image->buffer copies, no draw.
 *   CONSTPS : clear + constant-color fullscreen draw -> copy -> readback.
 *             Differs from CLEAR only by the draw. If CLEAR passes and
 *             CONSTPS fails, the draw/PS stage is the problem; if CLEAR
 *             already fails, the problem is at-or-after the RT.
 *   DELAY   : CLEAR repeated, then Sleep(2000) and Map again. If data
 *             appears only on the second read, the fence/submit wait
 *             returned early (premature completion).
 * Every image variant also dumps the FULL readback buffer (not just the
 * footprint rows) plus a nonzero-byte count, so "copy wrote nothing"
 * and "copy wrote at the wrong offset/stride" are distinguishable.
 *
 * Verdict lines are prefixed "rb12: VARIANT <name>"; the final
 * "rb12: RESULT 0" requires all variants to pass.
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o rb12.exe rb12.c -lole32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <string.h>

static void dbg(const char* s) { fprintf(stderr, "%s", s); fflush(stderr); }
#define FAIL(...) do { char b[512]; wsprintfA(b, __VA_ARGS__); dbg(b); \
    dbg("rb12: RESULT 1\n"); return 1; } while (0)

typedef HRESULT (WINAPI *PFN_D3D12_CREATE_DEVICE)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12_SERIALIZE_ROOT_SIG)(const D3D12_ROOT_SIGNATURE_DESC*,
        D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI *PFN_D3D_COMPILE)(const void*, SIZE_T, const char*, const void*, void*,
        const char*, const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);

static PFN_D3D_COMPILE pD3DCompile = NULL;

static ID3DBlob* compile_shader(const char* src, const char* target, const char* what)
{
    ID3DBlob* code = NULL; ID3DBlob* err = NULL;
    HRESULT hr = pD3DCompile(src, strlen(src), NULL, NULL, NULL, "main", target, 0, 0, &code, &err);
    if (FAILED(hr)) {
        char b[1024]; wsprintfA(b, "rb12: FAIL D3DCompile(%s, %s) hr=0x%08X err=%s\n", what, target, (unsigned)hr,
                  err ? (const char*)ID3D10Blob_GetBufferPointer(err) : "(none)");
        dbg(b);
        if (err) ID3D10Blob_Release(err);
        return NULL;
    }
    if (err) ID3D10Blob_Release(err);
    return code;
}

static void heap_start(ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE* out)
{
    ((void (STDMETHODCALLTYPE *)(ID3D12DescriptorHeap*, D3D12_CPU_DESCRIPTOR_HANDLE*))
        heap->lpVtbl->GetCPUDescriptorHandleForHeapStart)(heap, out);
}
static void res_desc(ID3D12Resource* res, D3D12_RESOURCE_DESC* out)
{
    ((void (STDMETHODCALLTYPE *)(ID3D12Resource*, D3D12_RESOURCE_DESC*))
        res->lpVtbl->GetDesc)(res, out);
}

static int wait_fence(ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64* value)
{
    UINT64 v = ++(*value);
    HRESULT hr = ID3D12CommandQueue_Signal(queue, fence, v);
    if (FAILED(hr)) return 0;
    if (ID3D12Fence_GetCompletedValue(fence) < v) {
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        ID3D12Fence_SetEventOnCompletion(fence, v, ev);
        WaitForSingleObject(ev, 30000);
        CloseHandle(ev);
    }
    return ID3D12Fence_GetCompletedValue(fence) >= v;
}

#define TW 4
#define TH 4

static ID3D12Resource* make_buffer(ID3D12Device* dev, D3D12_HEAP_TYPE heapType, UINT64 size,
                                   D3D12_RESOURCE_STATES initial, UINT flags, const char* what)
{
    D3D12_HEAP_PROPERTIES heap = {0}; heap.Type = heapType;
    D3D12_RESOURCE_DESC rd = {0};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = (D3D12_RESOURCE_FLAGS)flags;
    ID3D12Resource* res = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(dev, &heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                       initial, NULL, &IID_ID3D12Resource, (void**)&res);
    if (FAILED(hr)) { char b[256]; wsprintfA(b, "rb12: FAIL CreateCommittedResource(%s) hr=0x%08X\n", what, (unsigned)hr); dbg(b); return NULL; }
    return res;
}

static ID3D12Resource* make_upload_filled(ID3D12Device* dev, const void* data, UINT64 size, const char* what)
{
    ID3D12Resource* res = make_buffer(dev, D3D12_HEAP_TYPE_UPLOAD, size,
                                      D3D12_RESOURCE_STATE_GENERIC_READ, 0, what);
    if (!res) return NULL;
    void* mapped = NULL; D3D12_RANGE rr = {0, 0};
    HRESULT hr = ID3D12Resource_Map(res, 0, &rr, &mapped);
    if (FAILED(hr)) { ID3D12Resource_Release(res); return NULL; }
    memcpy(mapped, data, (size_t)size);
    ID3D12Resource_Unmap(res, 0, NULL);
    return res;
}

static void dump_hex(const unsigned char* p, unsigned int n, const char* tag)
{
    static const char* hexd = "0123456789abcdef";
    char line[128];
    char b[128];
    unsigned int off;
    for (off = 0; off < n; off += 16) {
        unsigned int i, pos = 0;
        wsprintfA(line, "rb12: HEX %s %u: ", tag, off);
        pos = strlen(line);
        for (i = 0; i < 16 && off + i < n; i++) {
            line[pos++] = hexd[(p[off + i] >> 4) & 0xF];
            line[pos++] = hexd[p[off + i] & 0xF];
            line[pos++] = ' ';
        }
        line[pos] = 0;
        wsprintfA(b, "%s\n", line);
        dbg(b);
    }
}

static unsigned int count_nonzero(const unsigned char* p, unsigned int n)
{
    unsigned int i, c = 0;
    for (i = 0; i < n; i++) if (p[i]) c++;
    return c;
}

struct ctx {
    ID3D12Device* dev; ID3D12CommandQueue* queue; ID3D12CommandAllocator* alloc;
    ID3D12Fence* fence; UINT64* fenceValue;
    ID3DBlob* vsBlob; PFN_D3D12_SERIALIZE_ROOT_SIG pSerialize;
};

static ID3D12GraphicsCommandList* new_list(struct ctx* cx, ID3D12PipelineState* pipe)
{
    ID3D12GraphicsCommandList* list = NULL;
    HRESULT hr = ID3D12Device_CreateCommandList(cx->dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                cx->alloc, pipe, &IID_ID3D12GraphicsCommandList, (void**)&list);
    if (FAILED(hr)) return NULL;
    return list;
}

static int exec_and_wait(struct ctx* cx, ID3D12GraphicsCommandList* list)
{
    HRESULT hr = ID3D12GraphicsCommandList_Close(list);
    if (FAILED(hr)) return 0;
    { ID3D12CommandList* lists[1] = { (ID3D12CommandList*)list };
      ID3D12CommandQueue_ExecuteCommandLists(cx->queue, 1, lists); }
    return wait_fence(cx->queue, cx->fence, cx->fenceValue);
}

/* ---------------- VARIANT: BUF (buffer copies only) ---------------- */
static int v_buf(struct ctx* cx)
{
    static unsigned char pat[64];
    int i;
    char b[512];
    for (i = 0; i < 64; i++) pat[i] = (unsigned char)(0x10 + i);
    ID3D12Resource* up = make_upload_filled(cx->dev, pat, 64, "buf upload");
    if (!up) { dbg("rb12: VARIANT BUF FAIL (upload create)\n"); return 0; }
    ID3D12Resource* def = make_buffer(cx->dev, D3D12_HEAP_TYPE_DEFAULT, 64,
                                      D3D12_RESOURCE_STATE_COPY_DEST, 0, "buf default");
    if (!def) { ID3D12Resource_Release(up); dbg("rb12: VARIANT BUF FAIL (default create)\n"); return 0; }
    ID3D12Resource* rb = make_buffer(cx->dev, D3D12_HEAP_TYPE_READBACK, 64,
                                     D3D12_RESOURCE_STATE_COPY_DEST, 0, "buf readback");
    if (!rb) { ID3D12Resource_Release(def); ID3D12Resource_Release(up); dbg("rb12: VARIANT BUF FAIL (readback create)\n"); return 0; }
    ID3D12GraphicsCommandList* list = new_list(cx, NULL);
    if (!list) { ID3D12Resource_Release(rb); ID3D12Resource_Release(def); ID3D12Resource_Release(up); dbg("rb12: VARIANT BUF FAIL (list create)\n"); return 0; }
    ID3D12GraphicsCommandList_CopyBufferRegion(list, def, 0, up, 0, 64);
    { D3D12_RESOURCE_BARRIER bar = {0};
      bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      bar.Transition.pResource = def;
      bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      bar.Transition.Subresource = 0;
      ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &bar); }
    ID3D12GraphicsCommandList_CopyBufferRegion(list, rb, 0, def, 0, 64);
    if (!exec_and_wait(cx, list)) { dbg("rb12: VARIANT BUF FAIL (exec/fence)\n"); ID3D12GraphicsCommandList_Release(list); ID3D12Resource_Release(rb); ID3D12Resource_Release(def); ID3D12Resource_Release(up); return 0; }
    ID3D12GraphicsCommandList_Release(list);
    { void* mapped = NULL; D3D12_RANGE rr = {0, 64};
      HRESULT hr = ID3D12Resource_Map(rb, 0, &rr, &mapped);
      int bad = 0, fi = -1, fg = 0;
      if (FAILED(hr)) { dbg("rb12: VARIANT BUF FAIL (readback Map)\n"); ID3D12Resource_Release(rb); ID3D12Resource_Release(def); ID3D12Resource_Release(up); return 0; }
      for (i = 0; i < 64; i++) if (((const unsigned char*)mapped)[i] != pat[i]) { bad++; if (fi < 0) { fi = i; fg = ((const unsigned char*)mapped)[i]; } }
      dump_hex((const unsigned char*)mapped, 64, "BUF");
      ID3D12Resource_Unmap(rb, 0, NULL);
      wsprintfA(b, "rb12: VARIANT BUF %s (bad=%d first idx=%d got=0x%02X want=0x%02X)\n",
                bad ? "FAIL" : "OK", bad, fi, fg, fi < 0 ? 0 : pat[fi]);
      dbg(b);
      ID3D12Resource_Release(rb); ID3D12Resource_Release(def); ID3D12Resource_Release(up);
      return bad == 0; }
}

/* ---------------- shared RT helpers ---------------- */
static ID3D12Resource* make_rt(struct ctx* cx, D3D12_RESOURCE_STATES initial)
{
    D3D12_HEAP_PROPERTIES heap = {0}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {0};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = TW; rd.Height = TH; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ID3D12Resource* res = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(cx->dev, &heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                       initial, NULL, &IID_ID3D12Resource, (void**)&res);
    if (FAILED(hr)) return NULL;
    return res;
}

static ID3D12DescriptorHeap* make_rtv(struct ctx* cx, ID3D12Resource* rt, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu)
{
    D3D12_DESCRIPTOR_HEAP_DESC rth = {0};
    rth.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rth.NumDescriptors = 1;
    ID3D12DescriptorHeap* heap = NULL;
    HRESULT hr = ID3D12Device_CreateDescriptorHeap(cx->dev, &rth, &IID_ID3D12DescriptorHeap, (void**)&heap);
    if (FAILED(hr)) return NULL;
    heap_start(heap, outCpu);
    ID3D12Device_CreateRenderTargetView(cx->dev, rt, NULL, *outCpu);
    return heap;
}

static int rt_footprint(struct ctx* cx, ID3D12Resource* rt, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* fp, UINT64* total)
{
    D3D12_RESOURCE_DESC d; res_desc(rt, &d);
    UINT rows = 0; UINT64 rowBytes = 0;
    ID3D12Device_GetCopyableFootprints(cx->dev, &d, 0, 1, 0, fp, &rows, &rowBytes, total);
    return 1;
}

static int check_rt(struct ctx* cx, ID3D12Resource* rb, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* fp,
                    UINT64 total, const unsigned char want[4], const char* name, int* outNonzero)
{
    char b[512];
    void* mapped = NULL; D3D12_RANGE rr = {0, (SIZE_T)total};
    HRESULT hr = ID3D12Resource_Map(rb, 0, &rr, &mapped);
    int bad = 0, fi = -1; unsigned char fg[4] = {0, 0, 0, 0};
    int y, x, ch;
    unsigned int nz;
    if (FAILED(hr)) { wsprintfA(b, "rb12: VARIANT %s FAIL (RT Map hr=0x%08X)\n", name, (unsigned)hr); dbg(b); return 0; }
    nz = count_nonzero((const unsigned char*)mapped, (unsigned int)total);
    if (outNonzero) *outNonzero = (int)nz;
    for (y = 0; y < TH; y++) for (x = 0; x < TW; x++) {
        const unsigned char* px = (const unsigned char*)mapped + fp->Footprint.RowPitch * y + x * 4;
        for (ch = 0; ch < 4; ch++)
            if (px[ch] != want[ch]) { bad++; if (fi < 0) { fi = x + y * TW; memcpy(fg, px, 4); } }
    }
    dump_hex((const unsigned char*)mapped, (unsigned int)total > 1024 ? 1024 : (unsigned int)total, name);
    ID3D12Resource_Unmap(rb, 0, NULL);
    wsprintfA(b, "rb12: VARIANT %s %s (bad=%d first idx=%d got=[%d %d %d %d] want=[%d %d %d %d] nonzero=%u/%u RowPitch=%u total=%u)\n",
              name, bad ? "FAIL" : "OK", bad, fi, fg[0], fg[1], fg[2], fg[3],
              want[0], want[1], want[2], want[3], nz, (unsigned int)total, fp->Footprint.RowPitch, (unsigned int)total);
    dbg(b);
    return bad == 0;
}

/* ---------------- VARIANT: CLEAR ---------------- */
static int v_clear(struct ctx* cx, int delaySecs)
{
    char b[512];
    ID3D12Resource* rt = make_rt(cx, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (!rt) { dbg("rb12: VARIANT CLEAR FAIL (RT create)\n"); return 0; }
    D3D12_CPU_DESCRIPTOR_HANDLE rtCpu;
    ID3D12DescriptorHeap* rtHeap = make_rtv(cx, rt, &rtCpu);
    if (!rtHeap) { ID3D12Resource_Release(rt); dbg("rb12: VARIANT CLEAR FAIL (RTV create)\n"); return 0; }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {0}; UINT64 total = 0;
    rt_footprint(cx, rt, &fp, &total);
    ID3D12Resource* rb = make_buffer(cx->dev, D3D12_HEAP_TYPE_READBACK, total,
                                     D3D12_RESOURCE_STATE_COPY_DEST, 0, "clear readback");
    if (!rb) { ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rt); dbg("rb12: VARIANT CLEAR FAIL (readback create)\n"); return 0; }
    ID3D12GraphicsCommandList* list = new_list(cx, NULL);
    if (!list) { ID3D12Resource_Release(rb); ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rt); dbg("rb12: VARIANT CLEAR FAIL (list create)\n"); return 0; }
    ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtCpu, FALSE, NULL);
    { D3D12_VIEWPORT vp = {0}; vp.Width = TW; vp.Height = TH; vp.MaxDepth = 1.0f;
      ID3D12GraphicsCommandList_RSSetViewports(list, 1, &vp);
      D3D12_RECT sc = {0, 0, TW, TH};
      ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &sc); }
    { const float clearCol[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
      ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtCpu, clearCol, 0, NULL); }
    { D3D12_RESOURCE_BARRIER bar = {0};
      bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      bar.Transition.pResource = rt;
      bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
      bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &bar); }
    { D3D12_TEXTURE_COPY_LOCATION rbDst = {0}, rtSrc = {0};
      rbDst.pResource = rb; rbDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; rbDst.PlacedFootprint = fp;
      rtSrc.pResource = rt; rtSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; rtSrc.SubresourceIndex = 0;
      ID3D12GraphicsCommandList_CopyTextureRegion(list, &rbDst, 0, 0, 0, &rtSrc, NULL); }
    if (!exec_and_wait(cx, list)) { dbg("rb12: VARIANT CLEAR FAIL (exec/fence)\n"); ID3D12GraphicsCommandList_Release(list); ID3D12Resource_Release(rb); ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rt); return 0; }
    ID3D12GraphicsCommandList_Release(list);
    { static const unsigned char want[4] = {255, 0, 255, 255};
      int nz = 0, ok = check_rt(cx, rb, &fp, total, want, "CLEAR", &nz);
      if (delaySecs > 0) {
          Sleep((DWORD)(delaySecs * 1000));
          { static const unsigned char want2[4] = {255, 0, 255, 255};
            int nz2 = 0, ok2 = check_rt(cx, rb, &fp, total, want2, "DELAY", &nz2);
            wsprintfA(b, "rb12: VARIANT DELAY %s (first=%s second=%s)\n", ok2 ? "OK" : "FAIL", ok ? "OK" : "FAIL", ok2 ? "OK" : "FAIL");
            dbg(b);
            ID3D12Resource_Release(rb); ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rt);
            return ok2; }
      }
      ID3D12Resource_Release(rb); ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rt);
      return ok; }
}

/* ---------------- VARIANT: UPLDTEX ---------------- */
static int v_upldtex(struct ctx* cx)
{
    char b[512];
    int idx, y, x;
    ID3D12Resource* rt = make_rt(cx, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!rt) { dbg("rb12: VARIANT UPLDTEX FAIL (RT create)\n"); return 0; }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {0}; UINT64 total = 0;
    rt_footprint(cx, rt, &fp, &total);
    ID3D12Resource* up = make_buffer(cx->dev, D3D12_HEAP_TYPE_UPLOAD, total,
                                     D3D12_RESOURCE_STATE_GENERIC_READ, 0, "upldtex upload");
    if (!up) { ID3D12Resource_Release(rt); dbg("rb12: VARIANT UPLDTEX FAIL (upload create)\n"); return 0; }
    { void* mapped = NULL; D3D12_RANGE rr = {0, 0};
      HRESULT hr = ID3D12Resource_Map(up, 0, &rr, &mapped);
      if (FAILED(hr)) { ID3D12Resource_Release(up); ID3D12Resource_Release(rt); dbg("rb12: VARIANT UPLDTEX FAIL (upload Map)\n"); return 0; }
      memset(mapped, 0, (size_t)total);
      for (y = 0; y < TH; y++) for (x = 0; x < TW; x++) {
          idx = x + y * TW;
          unsigned char* px = (unsigned char*)mapped + fp.Footprint.RowPitch * y + x * 4;
          px[0] = (unsigned char)(idx * 16); px[1] = (unsigned char)(255 - idx * 8);
          px[2] = (unsigned char)(idx * 4); px[3] = 255;
      }
      ID3D12Resource_Unmap(up, 0, NULL); }
    ID3D12Resource* rb = make_buffer(cx->dev, D3D12_HEAP_TYPE_READBACK, total,
                                     D3D12_RESOURCE_STATE_COPY_DEST, 0, "upldtex readback");
    if (!rb) { ID3D12Resource_Release(up); ID3D12Resource_Release(rt); dbg("rb12: VARIANT UPLDTEX FAIL (readback create)\n"); return 0; }
    ID3D12GraphicsCommandList* list = new_list(cx, NULL);
    if (!list) { ID3D12Resource_Release(rb); ID3D12Resource_Release(up); ID3D12Resource_Release(rt); dbg("rb12: VARIANT UPLDTEX FAIL (list create)\n"); return 0; }
    { D3D12_TEXTURE_COPY_LOCATION dstLoc = {0}, srcLoc = {0};
      dstLoc.pResource = rt; dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dstLoc.SubresourceIndex = 0;
      srcLoc.pResource = up; srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; srcLoc.PlacedFootprint = fp;
      ID3D12GraphicsCommandList_CopyTextureRegion(list, &dstLoc, 0, 0, 0, &srcLoc, NULL); }
    { D3D12_RESOURCE_BARRIER bar = {0};
      bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      bar.Transition.pResource = rt;
      bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &bar); }
    { D3D12_TEXTURE_COPY_LOCATION rbDst = {0}, rtSrc = {0};
      rbDst.pResource = rb; rbDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; rbDst.PlacedFootprint = fp;
      rtSrc.pResource = rt; rtSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; rtSrc.SubresourceIndex = 0;
      ID3D12GraphicsCommandList_CopyTextureRegion(list, &rbDst, 0, 0, 0, &rtSrc, NULL); }
    if (!exec_and_wait(cx, list)) { dbg("rb12: VARIANT UPLDTEX FAIL (exec/fence)\n"); ID3D12GraphicsCommandList_Release(list); ID3D12Resource_Release(rb); ID3D12Resource_Release(up); ID3D12Resource_Release(rt); return 0; }
    ID3D12GraphicsCommandList_Release(list);
    { void* mapped = NULL; D3D12_RANGE rr = {0, (SIZE_T)total};
      HRESULT hr = ID3D12Resource_Map(rb, 0, &rr, &mapped);
      int bad = 0, fi = -1; unsigned char fg[4] = {0, 0, 0, 0};
      unsigned int nz;
      if (FAILED(hr)) { dbg("rb12: VARIANT UPLDTEX FAIL (readback Map)\n"); ID3D12Resource_Release(rb); ID3D12Resource_Release(up); ID3D12Resource_Release(rt); return 0; }
      nz = count_nonzero((const unsigned char*)mapped, (unsigned int)total);
      for (y = 0; y < TH; y++) for (x = 0; x < TW; x++) {
          idx = x + y * TW;
          const unsigned char* px = (const unsigned char*)mapped + fp.Footprint.RowPitch * y + x * 4;
          unsigned char want[4];
          want[0] = (unsigned char)(idx * 16); want[1] = (unsigned char)(255 - idx * 8);
          want[2] = (unsigned char)(idx * 4); want[3] = 255;
          for (int ch = 0; ch < 4; ch++)
              if (px[ch] != want[ch]) { bad++; if (fi < 0) { fi = idx; memcpy(fg, px, 4); } }
      }
      dump_hex((const unsigned char*)mapped, (unsigned int)total > 1024 ? 1024 : (unsigned int)total, "UPLDTEX");
      ID3D12Resource_Unmap(rb, 0, NULL);
      wsprintfA(b, "rb12: VARIANT UPLDTEX %s (bad=%d first idx=%d got=[%d %d %d %d] nonzero=%u/%u RowPitch=%u total=%u)\n",
                bad ? "FAIL" : "OK", bad, fi, fg[0], fg[1], fg[2], fg[3], nz, (unsigned int)total, fp.Footprint.RowPitch, (unsigned int)total);
      dbg(b);
      ID3D12Resource_Release(rb); ID3D12Resource_Release(up); ID3D12Resource_Release(rt);
      return bad == 0; }
}

/* ---------------- VARIANT: CONSTPS ---------------- */
static int v_constps(struct ctx* cx)
{
    char b[512];
    static const char* PS =
        "struct PSIn { float4 pos : SV_Position; float4 col : COLOR; };\n"
        "float4 main(PSIn i) : SV_Target { return float4(0.75, 0.75, 0.875, 1.0); }\n";
    ID3DBlob* ps = compile_shader(PS, "ps_5_0", "const-ps");
    if (!ps) { dbg("rb12: VARIANT CONSTPS FAIL (PS compile)\n"); return 0; }
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    rsd.NumParameters = 0; rsd.pParameters = NULL;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* sigBlob = NULL; ID3DBlob* sigErr = NULL;
    HRESULT hr = cx->pSerialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &sigErr);
    if (FAILED(hr)) { ID3D10Blob_Release(ps); dbg("rb12: VARIANT CONSTPS FAIL (serialize empty root sig)\n"); return 0; }
    ID3D12RootSignature* rs = NULL;
    hr = ID3D12Device_CreateRootSignature(cx->dev, 0, ID3D10Blob_GetBufferPointer(sigBlob),
                                          ID3D10Blob_GetBufferSize(sigBlob), &IID_ID3D12RootSignature, (void**)&rs);
    ID3D10Blob_Release(sigBlob); if (sigErr) ID3D10Blob_Release(sigErr);
    if (FAILED(hr)) { ID3D10Blob_Release(ps); dbg("rb12: VARIANT CONSTPS FAIL (CreateRootSignature)\n"); return 0; }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {0};
    pso.pRootSignature = rs;
    pso.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(cx->vsBlob);
    pso.VS.BytecodeLength = ID3D10Blob_GetBufferSize(cx->vsBlob);
    pso.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps);
    pso.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
    static const D3D12_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    pso.InputLayout.pInputElementDescs = il;
    pso.InputLayout.NumElements = 2;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    ID3D12PipelineState* pipe = NULL;
    hr = ID3D12Device_CreateGraphicsPipelineState(cx->dev, &pso, &IID_ID3D12PipelineState, (void**)&pipe);
    if (FAILED(hr)) { ID3D12RootSignature_Release(rs); ID3D10Blob_Release(ps); wsprintfA(b, "rb12: VARIANT CONSTPS FAIL (CreateGraphicsPipelineState hr=0x%08X)\n", (unsigned)hr); dbg(b); return 0; }
    wsprintfA(b, "rb12: VARIANT CONSTPS PSO OK\n"); dbg(b);
    ID3D12Resource* rt = make_rt(cx, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (!rt) { ID3D12PipelineState_Release(pipe); ID3D12RootSignature_Release(rs); ID3D10Blob_Release(ps); dbg("rb12: VARIANT CONSTPS FAIL (RT create)\n"); return 0; }
    D3D12_CPU_DESCRIPTOR_HANDLE rtCpu;
    ID3D12DescriptorHeap* rtHeap = make_rtv(cx, rt, &rtCpu);
    if (!rtHeap) { ID3D12Resource_Release(rt); ID3D12PipelineState_Release(pipe); ID3D12RootSignature_Release(rs); ID3D10Blob_Release(ps); dbg("rb12: VARIANT CONSTPS FAIL (RTV create)\n"); return 0; }
    static const float verts[21] = {
        -1.0f, -1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
        -1.0f,  3.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
         3.0f, -1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
    };
    ID3D12Resource* vb = make_upload_filled(cx->dev, verts, sizeof(verts), "constps vb");
    if (!vb) { ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rt); ID3D12PipelineState_Release(pipe); ID3D12RootSignature_Release(rs); ID3D10Blob_Release(ps); dbg("rb12: VARIANT CONSTPS FAIL (vb create)\n"); return 0; }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {0}; UINT64 total = 0;
    rt_footprint(cx, rt, &fp, &total);
    ID3D12Resource* rb = make_buffer(cx->dev, D3D12_HEAP_TYPE_READBACK, total,
                                     D3D12_RESOURCE_STATE_COPY_DEST, 0, "constps readback");
    if (!rb) { ID3D12Resource_Release(vb); ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rt); ID3D12PipelineState_Release(pipe); ID3D12RootSignature_Release(rs); ID3D10Blob_Release(ps); dbg("rb12: VARIANT CONSTPS FAIL (readback create)\n"); return 0; }
    ID3D12GraphicsCommandList* list = new_list(cx, pipe);
    if (!list) { ID3D12Resource_Release(rb); ID3D12Resource_Release(vb); ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rt); ID3D12PipelineState_Release(pipe); ID3D12RootSignature_Release(rs); ID3D10Blob_Release(ps); dbg("rb12: VARIANT CONSTPS FAIL (list create)\n"); return 0; }
    ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtCpu, FALSE, NULL);
    { D3D12_VIEWPORT vp = {0}; vp.Width = TW; vp.Height = TH; vp.MaxDepth = 1.0f;
      ID3D12GraphicsCommandList_RSSetViewports(list, 1, &vp);
      D3D12_RECT sc = {0, 0, TW, TH};
      ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &sc); }
    { const float clearCol[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
      ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtCpu, clearCol, 0, NULL); }
    ID3D12GraphicsCommandList_SetPipelineState(list, pipe);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, rs);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    { D3D12_VERTEX_BUFFER_VIEW vbv = {0};
      vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
      vbv.SizeInBytes = sizeof(verts); vbv.StrideInBytes = 7 * sizeof(float);
      ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv); }
    ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
    { D3D12_RESOURCE_BARRIER bar = {0};
      bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      bar.Transition.pResource = rt;
      bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
      bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &bar); }
    { D3D12_TEXTURE_COPY_LOCATION rbDst = {0}, rtSrc = {0};
      rbDst.pResource = rb; rbDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; rbDst.PlacedFootprint = fp;
      rtSrc.pResource = rt; rtSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; rtSrc.SubresourceIndex = 0;
      ID3D12GraphicsCommandList_CopyTextureRegion(list, &rbDst, 0, 0, 0, &rtSrc, NULL); }
    if (!exec_and_wait(cx, list)) { dbg("rb12: VARIANT CONSTPS FAIL (exec/fence)\n"); ID3D12GraphicsCommandList_Release(list); ID3D12Resource_Release(rb); ID3D12Resource_Release(vb); ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rt); ID3D12PipelineState_Release(pipe); ID3D12RootSignature_Release(rs); ID3D10Blob_Release(ps); return 0; }
    ID3D12GraphicsCommandList_Release(list);
    { static const unsigned char want[4] = {191, 191, 223, 255};
      int nz = 0, ok = check_rt(cx, rb, &fp, total, want, "CONSTPS", &nz);
      ID3D12Resource_Release(rb); ID3D12Resource_Release(vb); ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rt); ID3D12PipelineState_Release(pipe); ID3D12RootSignature_Release(rs); ID3D10Blob_Release(ps);
      return ok; }
}

int main(void)
{
    char b[512];
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    if (!d3d12) FAIL("rb12: FAIL LoadLibrary(d3d12.dll) err=%lu\n", (unsigned long)GetLastError());
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!compiler) FAIL("rb12: FAIL LoadLibrary(d3dcompiler_47.dll) err=%lu\n", (unsigned long)GetLastError());
    pD3DCompile = (PFN_D3D_COMPILE)GetProcAddress(compiler, "D3DCompile");
    if (!pD3DCompile) FAIL("rb12: FAIL GetProcAddress(D3DCompile)\n");
    PFN_D3D12_CREATE_DEVICE pCreate =
        (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12, "D3D12CreateDevice");
    PFN_D3D12_SERIALIZE_ROOT_SIG pSerialize =
        (PFN_D3D12_SERIALIZE_ROOT_SIG)GetProcAddress(d3d12, "D3D12SerializeRootSignature");
    if (!pCreate || !pSerialize) FAIL("rb12: FAIL GetProcAddress(d3d12 exports)\n");

    ID3D12Device* dev = NULL;
    HRESULT hr = pCreate(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev);
    if (FAILED(hr)) FAIL("rb12: FAIL D3D12CreateDevice hr=0x%08X\n", (unsigned)hr);
    dbg("rb12: D3D12CreateDevice OK\n");

    D3D12_COMMAND_QUEUE_DESC qd = {0};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = NULL;
    hr = ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void**)&queue);
    if (FAILED(hr)) FAIL("rb12: FAIL CreateCommandQueue hr=0x%08X\n", (unsigned)hr);
    ID3D12CommandAllocator* alloc = NULL;
    hr = ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             &IID_ID3D12CommandAllocator, (void**)&alloc);
    if (FAILED(hr)) FAIL("rb12: FAIL CreateCommandAllocator hr=0x%08X\n", (unsigned)hr);
    ID3D12Fence* fence = NULL;
    hr = ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence);
    if (FAILED(hr)) FAIL("rb12: FAIL CreateFence hr=0x%08X\n", (unsigned)hr);
    UINT64 fenceValue = 0;

    static const char* VS_SRC =
        "struct VSIn { float3 pos : POSITION; float4 col : COLOR; };\n"
        "struct VSOut { float4 pos : SV_Position; float4 col : COLOR; };\n"
        "VSOut main(VSIn i) { VSOut o; o.pos = float4(i.pos, 1.0); o.col = i.col; return o; }\n";
    ID3DBlob* vsBlob = compile_shader(VS_SRC, "vs_5_0", "fullscreen-vs");
    if (!vsBlob) FAIL("rb12: FAIL fullscreen VS compile\n");

    struct ctx cx = { dev, queue, alloc, fence, &fenceValue, vsBlob, pSerialize };
    int bufOk = v_buf(&cx); ID3D12CommandAllocator_Reset(alloc);
    int clearOk = v_clear(&cx, 0); ID3D12CommandAllocator_Reset(alloc);
    int upldOk = v_upldtex(&cx); ID3D12CommandAllocator_Reset(alloc);
    int constOk = v_constps(&cx); ID3D12CommandAllocator_Reset(alloc);
    int delayOk = v_clear(&cx, 2); ID3D12CommandAllocator_Reset(alloc);

    wsprintfA(b, "rb12: SUMMARY buf=%s clear=%s upldtex=%s constps=%s delay=%s\n",
              bufOk ? "OK" : "FAIL", clearOk ? "OK" : "FAIL", upldOk ? "OK" : "FAIL",
              constOk ? "OK" : "FAIL", delayOk ? "OK" : "FAIL");
    dbg(b);
    ID3D10Blob_Release(vsBlob);
    ID3D12Fence_Release(fence);
    ID3D12CommandAllocator_Release(alloc);
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(dev);
    if (bufOk && clearOk && upldOk && constOk && delayOk) { dbg("rb12: RESULT 0\n"); return 0; }
    dbg("rb12: RESULT 1\n");
    return 1;
}
