/*
 * scene9.cpp — Complex D3D9 scene probe (28 draw calls/frame).
 *
 * Renders a "game-like" 2D scene: parallax background layer (8 quads),
 * mid-ground sprites (12 quads with alpha blending), foreground UI (8 quads).
 * Uses 3 procedural textures, alpha blending on sprite layer, different
 * render states per layer (blend on/off), indexed quads.
 *
 * Output: scene9: RESULT 0 + per-frame stats to stdout.
 *
 * Build: x86_64-w64-mingw32-g++ -O2 -o scene9.exe scene9.cpp -ld3d9 -lgdi32 -luser32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>


#define FRAMES 120
#define W 640
#define H 480
#define QUADS_BG 8
#define QUADS_MID 12
#define QUADS_FG 8
#define TOTAL_QUADS (QUADS_BG + QUADS_MID + QUADS_FG)
#define TEXW 64
#define TEXH 64

#define FAIL(...) do { printf(__VA_ARGS__); printf("scene9: RESULT 1\n"); fflush(stdout); return 1; } while (0)

typedef struct { float x, y, z, rhw; DWORD color; float u, v; } VERTEX;
#define FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

/* Three procedural textures: checker, gradient, stripes */
static void fill_checker(DWORD* px) {
    for (int y = 0; y < TEXH; y++) for (int x = 0; x < TEXW; x++) {
        int c = ((x / 8) + (y / 8)) & 1;
        BYTE v = c ? 220 : 40;
        px[y * TEXW + x] = 0xFF000000 | (v << 16) | (v << 8) | v;
    }
}
static void fill_gradient(DWORD* px) {
    for (int y = 0; y < TEXH; y++) for (int x = 0; x < TEXW; x++) {
        BYTE r = (BYTE)(x * 255 / TEXW), g = (BYTE)(y * 255 / TEXH), b = 128;
        px[y * TEXW + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
}
static void fill_stripes(DWORD* px) {
    for (int y = 0; y < TEXH; y++) for (int x = 0; x < TEXW; x++) {
        int s = (x / 4) & 1;
        BYTE r = s ? 255 : 0, g = s ? 100 : 150, b = s ? 50 : 200;
        BYTE a = (BYTE)(180 + (x * 75 / TEXW)); /* varying alpha */
        px[y * TEXW + x] = ((DWORD)a << 24) | (r << 16) | (g << 8) | b;
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE || m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

static IDirect3DTexture9* make_tex(IDirect3DDevice9* dev, void (*fill)(DWORD*)) {
    IDirect3DTexture9* tex = NULL;
    if (FAILED(IDirect3DDevice9_CreateTexture(dev, TEXW, TEXH, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED, &tex, NULL))) return NULL;
    D3DLOCKED_RECT lr;
    if (FAILED(IDirect3DTexture9_LockRect(tex, 0, &lr, NULL, 0))) { IDirect3DTexture9_Release(tex); return NULL; }
    DWORD px[TEXW * TEXH];
    fill(px);
    for (int y = 0; y < TEXH; y++)
        memcpy((BYTE*)lr.pBits + y * lr.Pitch, px + y * TEXW, TEXW * 4);
    IDirect3DTexture9_UnlockRect(tex, 0);
    return tex;
}

/* Draw one textured quad at (qx,qy) size (qw,qh) with color tint and uv scale */
static void draw_quad(IDirect3DDevice9* dev, IDirect3DVertexBuffer9* vb,
        float qx, float qy, float qw, float qh, DWORD color, float u0, float v0, float u1, float v1) {
    VERTEX* v = NULL;
    if (FAILED(IDirect3DVertexBuffer9_Lock(vb, 0, 4 * sizeof(VERTEX), (void**)&v, D3DLOCK_DISCARD))) return;
    v[0].x = qx;      v[0].y = qy;      v[0].z = 0.5f; v[0].rhw = 1.0f; v[0].color = color; v[0].u = u0; v[0].v = v0;
    v[1].x = qx + qw; v[1].y = qy;      v[1].z = 0.5f; v[1].rhw = 1.0f; v[1].color = color; v[1].u = u1; v[1].v = v0;
    v[2].x = qx;      v[2].y = qy + qh; v[2].z = 0.5f; v[2].rhw = 1.0f; v[2].color = color; v[2].u = u0; v[2].v = v1;
    v[3].x = qx + qw; v[3].y = qy + qh; v[3].z = 0.5f; v[3].rhw = 1.0f; v[3].color = color; v[3].u = u1; v[3].v = v1;
    IDirect3DVertexBuffer9_Unlock(vb);
    IDirect3DDevice9_SetStreamSource(dev, 0, vb, 0, sizeof(VERTEX));
    IDirect3DDevice9_SetFVF(dev, FVF);
    IDirect3DDevice9_DrawPrimitive(dev, D3DPT_TRIANGLESTRIP, 0, 2);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hPrev; (void)cmd; (void)show;
    printf("scene9: start (28 draw calls/frame, 3 textures, alpha blending)\n"); fflush(stdout);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = "scene9";
    RegisterClassA(&wc);
    HWND hwnd = 0;
    for (int i = 0; i < 120 && !hwnd; i++) {
        hwnd = CreateWindowA("scene9", "scene9", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT, W, H, 0, 0, hInst, 0);
        if (!hwnd) Sleep(500);
    }
    if (!hwnd) FAIL("scene9: FAIL CreateWindow\n");

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) FAIL("scene9: FAIL Direct3DCreate9 returned NULL\n");

    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = W; pp.BackBufferHeight = H;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* dev = NULL;
    if (FAILED(IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev)))
        FAIL("scene9: FAIL CreateDevice\n");

    IDirect3DTexture9* tex[3];
    tex[0] = make_tex(dev, fill_checker);
    tex[1] = make_tex(dev, fill_gradient);
    tex[2] = make_tex(dev, fill_stripes);
    if (!tex[0] || !tex[1] || !tex[2]) FAIL("scene9: FAIL texture creation\n");

    IDirect3DVertexBuffer9* vb = NULL;
    if (FAILED(IDirect3DDevice9_CreateVertexBuffer(dev, 4 * sizeof(VERTEX), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
            FVF, D3DPOOL_DEFAULT, &vb, NULL)))
        FAIL("scene9: FAIL CreateVertexBuffer\n");

    /* Sampler state: linear filtering, wrap */
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

    int draws = 0;
    for (int frame = 0; frame < FRAMES; frame++) {
        IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(20, 30, 50), 1.0f, 0);
        IDirect3DDevice9_BeginScene(dev);
        draws = 0;

        /* Layer 1: background — 8 quads, checker texture, no blending */
        IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, FALSE);
        IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9*)tex[0]);
        for (int i = 0; i < QUADS_BG; i++) {
            float px = (float)((i * 80 + frame * 2) % (W + 80)) - 40;
            float py = (float)(i * 55 % H);
            draw_quad(dev, vb, px, py, 96, 96, D3DCOLOR_XRGB(255,255,255), 0, 0, 2, 2);
            draws++;
        }

        /* Layer 2: sprites — 12 quads, stripes texture, alpha blending ON */
        IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9*)tex[2]);
        for (int i = 0; i < QUADS_MID; i++) {
            float a = (float)((frame * 7 + i * 53) % 360);
            float px = (float)(W/2 + ((frame * 3 + i * 47) % 360) - 180);
            float py = (float)(H/2 + ((frame * 2 + i * 31) % 260) - 130);
            DWORD tint = D3DCOLOR_ARGB(200, 255, 200 + (i * 4) % 55, 150);
            draw_quad(dev, vb, px, py, 64, 64, tint, 0, 0, 1, 1);
            draws++;
        }

        /* Layer 3: foreground UI — 8 quads, gradient texture, no blending */
        IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, FALSE);
        IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9*)tex[1]);
        for (int i = 0; i < QUADS_FG; i++) {
            float px = (float)(i * 78 + 8);
            float py = (float)(H - 48 + ((frame + i * 7) % 8) - 4);
            draw_quad(dev, vb, px, py, 70, 40, D3DCOLOR_XRGB(255,255,255), 0, 0, 1, 1);
            draws++;
        }

        IDirect3DDevice9_EndScene(dev);
        IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);

        if (frame % 30 == 0) {
            printf("scene9: frame %d draws=%d\n", frame, draws); fflush(stdout);
        }

        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
    }
done:
    printf("scene9: RESULT 0 (completed %d frames, %d draws/frame)\n", FRAMES, draws);
    fflush(stdout);
    return 0;
}
