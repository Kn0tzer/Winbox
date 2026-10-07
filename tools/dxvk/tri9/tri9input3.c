/*
 * tri9input3.c — comprehensive input verification game (D3D9 through DXVK).
 *
 * A real interactive test: renders a scene that responds visibly to input.
 * - Mouse move: crosshair follows cursor (absolute positioning)
 * - Left click: fires (muzzle flash + counter)
 * - Right click: toggles zoom (FOV indicator)
 * - Middle click: changes target color
 * - Wheel: adjusts crosshair size
 * - X1/X2: side button counters
 * - WASD/arrows: moves a player box
 * - Space: jump (box bounces)
 *
 * All input logged to stderr as:
 *   input3: MOUSE x,y
 *   input3: BTN n DOWN|UP
 *   input3: WHEEL dx,dy
 *   input3: KEYDOWN vk=0x.. keys=N
 *   input3: KEYUP vk=0x..
 *   input3: frame N mx,my shots=N
 *   input3: RESULT 0
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o tri9input3.exe tri9input3.c -ld3d9 -lgdi32 -luser32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

#define W 480
#define H 360

static void dbg(const char* s) { fprintf(stderr, "%s", s); fflush(stderr); }
#define FAIL(...) do { char b[256]; wsprintfA(b, __VA_ARGS__); dbg(b); \
    dbg("input3: RESULT 1\n"); return 1; } while (0)

typedef struct { float x, y, z, rhw; DWORD color; } V;
#define FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

/* game state */
static int g_mx = 240, g_my = 180;      /* mouse pos */
static int g_shots = 0, g_zoom = 0;      /* left clicks, right toggle */
static int g_wheelAcc = 0;              /* wheel accumulator */
static int g_x1 = 0, g_x2 = 0, g_mid = 0;
static int g_px = 240, g_py = 300;      /* player box pos */
static int g_vy = 0;                    /* jump velocity */
static int g_keys = 0, g_btns = 0, g_moves = 0, g_wheels = 0;
static DWORD g_targetColor = 0xffff0000;
static int g_keyState[256] = {0};

static void quad(V* v, float x, float y, float w, float h, DWORD c) {
    v[0].x=x; v[0].y=y; v[0].z=0.5f; v[0].rhw=1.0f; v[0].color=c;
    v[1].x=x+w; v[1].y=y; v[1].z=0.5f; v[1].rhw=1.0f; v[1].color=c;
    v[2].x=x; v[2].y=y+h; v[2].z=0.5f; v[2].rhw=1.0f; v[2].color=c;
    v[3].x=x+w; v[3].y=y; v[3].z=0.5f; v[3].rhw=1.0f; v[3].color=c;
    v[4].x=x+w; v[4].y=y+h; v[4].z=0.5f; v[4].rhw=1.0f; v[4].color=c;
    v[5].x=x; v[5].y=y+h; v[5].z=0.5f; v[5].rhw=1.0f; v[5].color=c;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    char b[160];
    switch (m) {
    case WM_MOUSEMOVE:
        g_mx = (short)LOWORD(l); g_my = (short)HIWORD(l); g_moves++;
        if (g_moves % 10 == 1) {
            wsprintfA(b, "input3: MOUSE %d,%d moves=%d\n", g_mx, g_my, g_moves);
            dbg(b);
        }
        return 0;
    case WM_LBUTTONDOWN: g_shots++; g_btns++;
        wsprintfA(b, "input3: BTN 1 DOWN shots=%d\n", g_shots); dbg(b); return 0;
    case WM_LBUTTONUP:
        wsprintfA(b, "input3: BTN 1 UP\n"); dbg(b); return 0;
    case WM_RBUTTONDOWN: g_zoom = !g_zoom; g_btns++;
        wsprintfA(b, "input3: BTN 3 DOWN zoom=%d\n", g_zoom); dbg(b); return 0;
    case WM_RBUTTONUP:
        wsprintfA(b, "input3: BTN 3 UP\n"); dbg(b); return 0;
    case WM_MBUTTONDOWN: g_mid++; g_btns++;
        g_targetColor = (g_targetColor == 0xffff0000) ? 0xff00ff00 : 0xffff0000;
        wsprintfA(b, "input3: BTN 2 DOWN mid=%d\n", g_mid); dbg(b); return 0;
    case WM_MBUTTONUP:
        wsprintfA(b, "input3: BTN 2 UP\n"); dbg(b); return 0;
    case WM_XBUTTONDOWN: {
        int xb = HIWORD(w);
        if (xb == XBUTTON1) { g_x1++; wsprintfA(b, "input3: BTN 8 DOWN x1=%d\n", g_x1); }
        else { g_x2++; wsprintfA(b, "input3: BTN 9 DOWN x2=%d\n", g_x2); }
        g_btns++; dbg(b); return 0;
    }
    case WM_XBUTTONUP: {
        int xb = HIWORD(w);
        wsprintfA(b, "input3: BTN %d UP\n", xb == XBUTTON1 ? 8 : 9); dbg(b); return 0;
    }
    case WM_MOUSEWHEEL: {
        int d = (short)HIWORD(w);
        g_wheelAcc += d; g_wheels++;
        wsprintfA(b, "input3: WHEEL 0,%d acc=%d\n", d, g_wheelAcc); dbg(b); return 0;
    }
    case WM_MOUSEHWHEEL: {
        int d = (short)HIWORD(w);
        g_wheels++;
        wsprintfA(b, "input3: WHEEL %d,0 acc=%d\n", d, g_wheelAcc); dbg(b); return 0;
    }
    case WM_KEYDOWN: {
        int vk = (int)w;
        if (vk >= 0 && vk < 256) g_keyState[vk] = 1;
        g_keys++;
        if (vk == VK_SPACE && g_py >= 300) g_vy = -12;
        if (vk == VK_ESCAPE) { PostQuitMessage(0); return 0; }
        wsprintfA(b, "input3: KEYDOWN vk=0x%02x keys=%d\n", vk, g_keys); dbg(b);
        return 0;
    }
    case WM_KEYUP: {
        int vk = (int)w;
        if (vk >= 0 && vk < 256) g_keyState[vk] = 0;
        wsprintfA(b, "input3: KEYUP vk=0x%02x\n", vk); dbg(b);
        return 0;
    }
    case WM_CLOSE: case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    (void)hp; (void)cmd; (void)show;
    dbg("input3: WinMain start\n");
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = "input3";
    dbg("input3: registering class\n");
    RegisterClassA(&wc);
    dbg("input3: creating window\n");
    HWND hwnd = CreateWindowA("input3", "Input Test 3", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, W, H, 0, 0, hi, 0);
    if (!hwnd) FAIL("input3: FAIL CreateWindow\n");
    dbg("input3: window OK\n");

    dbg("input3: calling Direct3DCreate9\n");
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) FAIL("input3: FAIL Direct3DCreate9\n");
    dbg("input3: Direct3DCreate9 OK\n");
    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = W; pp.BackBufferHeight = H;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev = NULL;
    HRESULT hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) FAIL("input3: FAIL CreateDevice hr=0x%08lx\n", hr);
    dbg("input3: device OK, entering game loop\n");
    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);

    MSG msg; int frames = 0;
    V verts[6*8];
    for (;;) {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        /* player movement from held keys */
        if (g_keyState['W'] || g_keyState[VK_UP]) g_py -= 3;
        if (g_keyState['S'] || g_keyState[VK_DOWN]) g_py += 3;
        if (g_keyState['A'] || g_keyState[VK_LEFT]) g_px -= 3;
        if (g_keyState['D'] || g_keyState[VK_RIGHT]) g_px += 3;
        if (g_px < 20) g_px = 20; if (g_px > W-20) g_px = W-20;
        /* gravity / jump */
        if (g_py < 300 || g_vy != 0) { g_vy += 1; g_py += g_vy; if (g_py >= 300) { g_py = 300; g_vy = 0; } }

        IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(10, 20, 40), 1.0f, 0);
        if (SUCCEEDED(IDirect3DDevice9_BeginScene(dev))) {
            int n = 0;
            /* target (changes color on middle click) */
            quad(verts + n*6, 200, 100, 80, 80, g_targetColor); n++;
            /* player box (WASD/arrows + jump) */
            quad(verts + n*6, (float)g_px-15, (float)g_py-15, 30, 30, 0xff00ff00); n++;
            /* crosshair at mouse pos (size from wheel) */
            int cs = 10 + (g_wheelAcc / 120) * 2;
            if (cs < 4) cs = 4; if (cs > 40) cs = 40;
            DWORD chc = g_zoom ? 0xffffff00 : 0xffffffff;
            quad(verts + n*6, (float)g_mx-cs, (float)g_my-1, (float)cs*2, 2, chc); n++;
            quad(verts + n*6, (float)g_mx-1, (float)g_my-cs, 2, (float)cs*2, chc); n++;
            /* muzzle flash on recent shot */
            if (g_shots > 0 && (frames % 30) < 5) {
                quad(verts + n*6, (float)g_mx-6, (float)g_my-6, 12, 12, 0xffff8800); n++;
            }
            IDirect3DDevice9_SetFVF(dev, FVF);
            IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, n*2, verts, sizeof(V));
            IDirect3DDevice9_EndScene(dev);
        }
        IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
        frames++;
        if (frames % 120 == 0) {
            char b[128];
            wsprintfA(b, "input3: frame %d mouse=%d,%d shots=%d keys=%d btns=%d moves=%d wheels=%d\n",
                frames, g_mx, g_my, g_shots, g_keys, g_btns, g_moves, g_wheels);
            dbg(b);
        }
        if (frames >= 3600) break;
        Sleep(16);
    }
done: {
    char b[128];
    wsprintfA(b, "input3: SUMMARY frames=%d mouse_moves=%d shots=%d keys=%d btns=%d wheels=%d x1=%d x2=%d\n",
        frames, g_moves, g_shots, g_keys, g_btns, g_wheels, g_x1, g_x2);
    dbg(b);
    dbg("input3: RESULT 0\n");
}
    IDirect3DDevice9_Release(dev);
    IDirect3D9_Release(d3d);
    return 0;
}
