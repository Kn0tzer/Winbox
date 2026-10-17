/*
 * d3dconf9.c -- D3D9 conformance matrix (64-bit, DXVK d3d9.dll).
 *
 * Exercises the D3D9 API surface beyond the single-triangle probes:
 * device creation, format checks, VB/IB, indexed + textured draws,
 * depth-stencil match, a hand-assembled vs_1_1 shader (bonus op), Present.
 *
 * Machine-readable log on stderr:
 *   d3dconf9: OP <name> RESULT <OK|FAIL|SKIP> hr=0x%08X ms=%u
 *   d3dconf9: DONE ok=N fail=M skip=K
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o d3dconf9.exe d3dconf9.c -ld3d9 -lgdi32 -luser32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

#define W 480
#define H 360

static int g_ok = 0, g_fail = 0, g_skip = 0;

static void op(const char* name, int ok, HRESULT hr, DWORD ms) {
    char b[256];
    wsprintfA(b, "d3dconf9: OP %-22s RESULT %s hr=0x%08X ms=%u\n",
              name, ok ? "OK" : "FAIL", (unsigned)hr, (unsigned)ms);
    fprintf(stderr, "%s", b); fflush(stderr);
    OutputDebugStringA(b);
    if (ok) g_ok++; else g_fail++;
}

typedef struct { float x, y, z, rhw; DWORD color; } V3;
#define FVF3 (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)
typedef struct { float x, y, z, rhw; DWORD color; float u, v; } V3T;
#define FVF3T (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)
typedef struct { float x, y, z; DWORD color; } VSIN;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE || m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hPrev; (void)cmd; (void)show;
    DWORD t0, t1; HRESULT hr; int i;
    fprintf(stderr, "d3dconf9: start\n"); fflush(stderr);

    t0 = GetTickCount();
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    op("d3d9_create", d3d != NULL, d3d ? S_OK : E_FAIL, GetTickCount() - t0);
    if (!d3d) goto done;

    struct { const char* name; D3DFORMAT fmt; } fmts[] = {
        { "checkfmt_X8R8G8B8", D3DFMT_X8R8G8B8 },
        { "checkfmt_A8R8G8B8", D3DFMT_A8R8G8B8 },
        { "checkfmt_R5G6B5",   D3DFMT_R5G6B5 },
        { "checkfmt_X1R5G5B5", D3DFMT_X1R5G5B5 },
    };
    for (i = 0; i < 4; i++) {
        t0 = GetTickCount();
        hr = IDirect3D9_CheckDeviceType(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                       fmts[i].fmt, fmts[i].fmt, TRUE);
        op(fmts[i].name, SUCCEEDED(hr), hr, GetTickCount() - t0);
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = "d3dconf9";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("d3dconf9", "d3dconf9", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, W, H, 0, 0, hInst, 0);
    if (!hwnd) { op("create_window", 0, E_FAIL, 0); goto done; }

    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = W; pp.BackBufferHeight = H;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* dev = NULL;
    t0 = GetTickCount();
    hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    t1 = GetTickCount() - t0;
    op("device_HAL_D16", SUCCEEDED(hr), hr, t1);
    if (FAILED(hr)) goto done;

    V3 verts[] = {
        { 240.0f,  60.0f, 0.5f, 1.0f, 0xffff0000 },
        { 420.0f, 300.0f, 0.5f, 1.0f, 0xff00ff00 },
        {  60.0f, 300.0f, 0.5f, 1.0f, 0xff0000ff },
    };
    IDirect3DVertexBuffer9* vb = NULL; void* pv = NULL;
    t0 = GetTickCount();
    hr = IDirect3DDevice9_CreateVertexBuffer(dev, sizeof(verts), 0, FVF3,
                                             D3DPOOL_DEFAULT, &vb, NULL);
    if (SUCCEEDED(hr)) {
        hr = IDirect3DVertexBuffer9_Lock(vb, 0, sizeof(verts), &pv, 0);
        if (SUCCEEDED(hr)) { CopyMemory(pv, verts, sizeof(verts));
            hr = IDirect3DVertexBuffer9_Unlock(vb); }
    }
    op("vb_create_fill", SUCCEEDED(hr), hr, GetTickCount() - t0);

    WORD idx[] = { 0, 1, 2 };
    IDirect3DIndexBuffer9* ib = NULL;
    t0 = GetTickCount();
    hr = IDirect3DDevice9_CreateIndexBuffer(dev, sizeof(idx), 0, D3DFMT_INDEX16,
                                            D3DPOOL_DEFAULT, &ib, NULL);
    if (SUCCEEDED(hr)) {
        hr = IDirect3DIndexBuffer9_Lock(ib, 0, sizeof(idx), &pv, 0);
        if (SUCCEEDED(hr)) { CopyMemory(pv, idx, sizeof(idx));
            hr = IDirect3DIndexBuffer9_Unlock(ib); }
    }
    op("ib_create_fill", SUCCEEDED(hr), hr, GetTickCount() - t0);

    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);

    t0 = GetTickCount();
    IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                           D3DCOLOR_XRGB(0, 51, 102), 1.0f, 0);
    hr = S_OK;
    if (SUCCEEDED(IDirect3DDevice9_BeginScene(dev))) {
        IDirect3DDevice9_SetStreamSource(dev, 0, vb, 0, sizeof(V3));
        IDirect3DDevice9_SetIndices(dev, ib);
        IDirect3DDevice9_SetFVF(dev, FVF3);
        hr = IDirect3DDevice9_DrawIndexedPrimitive(dev, D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
        IDirect3DDevice9_EndScene(dev);
    } else hr = E_FAIL;
    op("draw_indexed", SUCCEEDED(hr), hr, GetTickCount() - t0);

    t0 = GetTickCount();
    hr = IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
    op("present_first", SUCCEEDED(hr), hr, GetTickCount() - t0);

    IDirect3DTexture9* tex = NULL;
    t0 = GetTickCount();
    hr = IDirect3DDevice9_CreateTexture(dev, 64, 64, 1, 0, D3DFMT_A8R8G8B8,
                                        D3DPOOL_DEFAULT, &tex, NULL);
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(hr) && SUCCEEDED(IDirect3DTexture9_LockRect(tex, 0, &lr, NULL, 0))) {
        int x, y;
        for (y = 0; y < 64; y++) for (x = 0; x < 64; x++)
            ((DWORD*)((BYTE*)lr.pBits + y * lr.Pitch))[x] =
                0xff000000 | ((x * 4) << 16) | ((y * 4) << 8) | 0x80;
        hr = IDirect3DTexture9_UnlockRect(tex, 0);
    }
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9*)tex);
    op("tex_create_set", SUCCEEDED(hr), hr, GetTickCount() - t0);

    V3T tv[] = {
        {  60.0f,  60.0f, 0.5f, 1.0f, 0xffffffff, 0.0f, 0.0f },
        { 180.0f,  60.0f, 0.5f, 1.0f, 0xffffffff, 1.0f, 0.0f },
        { 180.0f, 180.0f, 0.5f, 1.0f, 0xffffffff, 1.0f, 1.0f },
        {  60.0f, 180.0f, 0.5f, 1.0f, 0xffffffff, 0.0f, 1.0f },
    };
    IDirect3DVertexBuffer9* tvb = NULL;
    t0 = GetTickCount();
    hr = IDirect3DDevice9_CreateVertexBuffer(dev, sizeof(tv), 0, FVF3T,
                                             D3DPOOL_DEFAULT, &tvb, NULL);
    if (SUCCEEDED(hr)) {
        hr = IDirect3DVertexBuffer9_Lock(tvb, 0, sizeof(tv), &pv, 0);
        if (SUCCEEDED(hr)) { CopyMemory(pv, tv, sizeof(tv));
            hr = IDirect3DVertexBuffer9_Unlock(tvb); }
    }
    if (SUCCEEDED(hr) && SUCCEEDED(IDirect3DDevice9_BeginScene(dev))) {
        IDirect3DDevice9_SetStreamSource(dev, 0, tvb, 0, sizeof(V3T));
        IDirect3DDevice9_SetFVF(dev, FVF3T);
        hr = IDirect3DDevice9_DrawPrimitive(dev, D3DPT_TRIANGLEFAN, 0, 2);
        IDirect3DDevice9_EndScene(dev);
    } else if (SUCCEEDED(hr)) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
    op("draw_textured", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (tvb) IDirect3DVertexBuffer9_Release(tvb);

    t0 = GetTickCount();
    hr = IDirect3D9_CheckDepthStencilMatch(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                           D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8,
                                           D3DFMT_D24X8);
    op("depth_D24X8_match", SUCCEEDED(hr), hr, GetTickCount() - t0);

    /* Bonus: hand-assembled vs_1_1 passthrough (position+color).
     * Token encoding per D3D9 shader token spec; a FAIL here is a
     * test-asset suspect first, not a bridge bug. */
    static const DWORD vs11[] = {
        0xFFFE0101,
        0x0000F800, 0x100F0000,
        0x0000F80A, 0x100F0001,
        0x00000800, 0x700F0000, 0x100000E4,
        0x00000800, 0x800F0000, 0x100000E5,
        0x0000FFFF
    };
    IDirect3DVertexShader9* vs = NULL;
    t0 = GetTickCount();
    hr = IDirect3DDevice9_CreateVertexShader(dev, vs11, &vs);
    VSIN vsv[] = {
        { -0.5f, -0.5f, 0.5f, 0xffffff00 },
        {  0.5f, -0.5f, 0.5f, 0xff00ffff },
        {  0.0f,  0.5f, 0.5f, 0xffff00ff },
    };
    IDirect3DVertexBuffer9* vsvb = NULL;
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice9_CreateVertexBuffer(dev, sizeof(vsv), 0,
                D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &vsvb, NULL);
    if (SUCCEEDED(hr)) {
        hr = IDirect3DVertexBuffer9_Lock(vsvb, 0, sizeof(vsv), &pv, 0);
        if (SUCCEEDED(hr)) { CopyMemory(pv, vsv, sizeof(vsv));
            hr = IDirect3DVertexBuffer9_Unlock(vsvb); }
    }
    if (SUCCEEDED(hr) && SUCCEEDED(IDirect3DDevice9_BeginScene(dev))) {
        IDirect3DDevice9_SetVertexShader(dev, vs);
        IDirect3DDevice9_SetStreamSource(dev, 0, vsvb, 0, sizeof(VSIN));
        hr = IDirect3DDevice9_DrawPrimitive(dev, D3DPT_TRIANGLELIST, 0, 1);
        IDirect3DDevice9_EndScene(dev);
        IDirect3DDevice9_SetVertexShader(dev, NULL);
    } else if (SUCCEEDED(hr)) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
    op("vs_handasm_bonus", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (vsvb) IDirect3DVertexBuffer9_Release(vsvb);
    if (vs) IDirect3DVertexShader9_Release(vs);

    t0 = GetTickCount();
    hr = S_OK; int f;
    for (f = 0; f < 30; f++) {
        IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                               D3DCOLOR_XRGB(0, 51, 102), 1.0f, 0);
        if (SUCCEEDED(IDirect3DDevice9_BeginScene(dev))) {
            IDirect3DDevice9_SetStreamSource(dev, 0, vb, 0, sizeof(V3));
            IDirect3DDevice9_SetIndices(dev, ib);
            IDirect3DDevice9_SetFVF(dev, FVF3);
            IDirect3DDevice9_DrawIndexedPrimitive(dev, D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
            IDirect3DDevice9_EndScene(dev);
        }
        hr = IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
        if (FAILED(hr)) break;
        { MSG msg; while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto ploop_done;
            TranslateMessage(&msg); DispatchMessage(&msg); } }
    }
ploop_done:
    t1 = GetTickCount() - t0;
    { char b[128]; wsprintfA(b, "d3dconf9: present_30f frames=%d total_ms=%u\n",
                             f, (unsigned)t1);
      fprintf(stderr, "%s", b); fflush(stderr); }
    op("present_30f", SUCCEEDED(hr) && f == 30, hr, t1);

    if (tex) IDirect3DTexture9_Release(tex);
    if (ib) IDirect3DIndexBuffer9_Release(ib);
    if (vb) IDirect3DVertexBuffer9_Release(vb);
    if (dev) IDirect3DDevice9_Release(dev);
done:
    if (d3d) IDirect3D9_Release(d3d);
    { char b[128]; wsprintfA(b, "d3dconf9: DONE ok=%d fail=%d skip=%d\n",
                             g_ok, g_fail, g_skip);
      fprintf(stderr, "%s", b); fflush(stderr); OutputDebugStringA(b); }
    return g_fail ? 1 : 0;
}
