/*
 * fpsinput.c — FPS-style input verification game (D3D9 through DXVK).
 *
 * A real interactive game testing the input paths Portal needs:
 * - WASD/arrows: move player (multiple simultaneous keys)
 * - Relative mouse: FPS-style aim rotation (yaw/pitch from mouse deltas)
 * - Left click: shoot projectile in aim direction
 * - Right click: toggle zoom
 * - Space: dash
 *
 * This is the critical Portal input path: FPS mouse-look via relative
 * mouse deltas. The game computes aim angle from WM_MOUSEMOVE deltas,
 * exactly like a real FPS does.
 *
 * Machine-readable diagnostics on stderr:
 *   fpsinput: LOOK yaw=X pitch=Y mdx=N mdy=N   (aim changed by mouse)
 *   fpsinput: MOVE x=X y=Y keys=WK            (player moved)
 *   fpsinput: SHOT n=X tx=Y ty=Z             (projectile fired)
 *   fpsinput: HIT n=X score=Y                (target hit)
 *   fpsinput: KEYDOWN vk=0x.. keys=N
 *   fpsinput: KEYUP vk=0x..
 *   fpsinput: BTN n DOWN|UP
 *   fpsinput: frame N px=X py=Y yaw=Y shots=S score=C
 *   fpsinput: RESULT 0
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o fpsinput.exe fpsinput.c -ld3d9 -lgdi32 -luser32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <math.h>

#define W 640
#define H 480
#define FRAMES 3600  /* 60 seconds at 60fps */

static void dbg(const char* s) { fprintf(stderr, "%s", s); fflush(stderr); }

typedef struct { float x, y, z, rhw; DWORD color; } V;
#define FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

/* ---- game state ---- */
static float g_px = 320.0f, g_py = 400.0f;  /* player pos */
static float g_yaw = -1.5708f;               /* aim angle, radians (up = -PI/2) */
static float g_pitch = 0.0f;                 /* aim pitch (for logging) */
static int g_shots = 0, g_hits = 0, g_score = 0;
static int g_keys = 0, g_looks = 0, g_moves = 0;
static int g_keyState[256] = {0};
static int g_lastMX = -1, g_lastMY = -1;     /* for delta computation */
static int g_frame = 0;

/* projectiles */
#define MAXP 32
static struct { float x, y, vx, vy; int live; } g_proj[MAXP];
/* targets */
#define MAXT 5
static struct { float x, y; int live; } g_tgt[MAXT];

static void quad(V* v, float x, float y, float w, float h, DWORD c) {
    v[0].x=x; v[0].y=y; v[0].z=0.5f; v[0].rhw=1.0f; v[0].color=c;
    v[1].x=x+w; v[1].y=y; v[1].z=0.5f; v[1].rhw=1.0f; v[1].color=c;
    v[2].x=x; v[2].y=y+h; v[2].z=0.5f; v[2].rhw=1.0f; v[2].color=c;
    v[3].x=x+w; v[3].y=y; v[3].z=0.5f; v[3].rhw=1.0f; v[3].color=c;
    v[4].x=x+w; v[4].y=y+h; v[4].z=0.5f; v[4].rhw=1.0f; v[4].color=c;
    v[5].x=x; v[5].y=y+h; v[5].z=0.5f; v[5].rhw=1.0f; v[5].color=c;
}

static void spawnTargets(void) {
    for (int i = 0; i < MAXT; i++) {
        g_tgt[i].x = 80.0f + (i * 120.0f);
        g_tgt[i].y = 80.0f + ((i % 2) * 60.0f);
        g_tgt[i].live = 1;
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    char b[192];
    switch (m) {
    case WM_MOUSEMOVE: {
        /* FPS-style: compute delta from last position, rotate aim.
         * This is exactly what Portal does with relative mouse input. */
        int mx = (short)LOWORD(l), my = (short)HIWORD(l);
        if (g_lastMX >= 0) {
            int dx = mx - g_lastMX, dy = my - g_lastMY;
            if (dx != 0 || dy != 0) {
                g_yaw += dx * 0.005f;
                g_pitch += dy * 0.005f;
                if (g_pitch > 1.2f) g_pitch = 1.2f;
                if (g_pitch < -1.2f) g_pitch = -1.2f;
                g_looks++;
                if (g_looks % 5 == 1) {
                    wsprintfA(b, "fpsinput: LOOK yaw=%.3f pitch=%.3f mdx=%d mdy=%d looks=%d\n",
                              g_yaw, g_pitch, dx, dy, g_looks);
                    dbg(b);
                }
            }
        }
        g_lastMX = mx; g_lastMY = my;
        return 0;
    }
    case WM_LBUTTONDOWN: {
        /* shoot in aim direction */
        for (int i = 0; i < MAXP; i++) {
            if (!g_proj[i].live) {
                g_proj[i].x = g_px; g_proj[i].y = g_py;
                g_proj[i].vx = cosf(g_yaw) * 8.0f;
                g_proj[i].vy = sinf(g_yaw) * 8.0f;
                g_proj[i].live = 1;
                g_shots++;
                wsprintfA(b, "fpsinput: SHOT n=%d yaw=%.3f\n", g_shots, g_yaw);
                dbg(b);
                break;
            }
        }
        wsprintfA(b, "fpsinput: BTN 1 DOWN shots=%d\n", g_shots); dbg(b);
        return 0;
    }
    case WM_LBUTTONUP:
        dbg("fpsinput: BTN 1 UP\n"); return 0;
    case WM_RBUTTONDOWN:
        dbg("fpsinput: BTN 3 DOWN\n"); return 0;
    case WM_RBUTTONUP:
        dbg("fpsinput: BTN 3 UP\n"); return 0;
    case WM_KEYDOWN: {
        int vk = (int)w;
        if (vk >= 0 && vk < 256 && !g_keyState[vk]) {
            g_keyState[vk] = 1; g_keys++;
            wsprintfA(b, "fpsinput: KEYDOWN vk=0x%02x keys=%d\n", vk, g_keys);
            dbg(b);
        }
        if (vk == VK_ESCAPE) { PostQuitMessage(0); return 0; }
        return 0;
    }
    case WM_KEYUP: {
        int vk = (int)w;
        if (vk >= 0 && vk < 256) {
            g_keyState[vk] = 0;
            wsprintfA(b, "fpsinput: KEYUP vk=0x%02x\n", vk);
            dbg(b);
        }
        return 0;
    }
    case WM_CLOSE:
    case WM_DESTROY:
        PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static void step(void) {
    char b[192];
    /* WASD + arrows movement (multiple simultaneous keys) */
    float dx = 0, dy = 0;
    if (g_keyState['W'] || g_keyState[VK_UP]) dy -= 3.0f;
    if (g_keyState['S'] || g_keyState[VK_DOWN]) dy += 3.0f;
    if (g_keyState['A'] || g_keyState[VK_LEFT]) dx -= 3.0f;
    if (g_keyState['D'] || g_keyState[VK_RIGHT]) dx += 3.0f;
    /* Space = dash in aim direction */
    if (g_keyState[VK_SPACE]) {
        dx += cosf(g_yaw) * 6.0f;
        dy += sinf(g_yaw) * 6.0f;
    }
    if (dx != 0 || dy != 0) {
        g_px += dx; g_py += dy;
        if (g_px < 20) g_px = 20; if (g_px > W-20) g_px = W-20;
        if (g_py < 20) g_py = 20; if (g_py > H-20) g_py = H-20;
        g_moves++;
        if (g_moves % 30 == 1) {
            int wk = (g_keyState['W']?8:0)|(g_keyState['A']?4:0)|
                     (g_keyState['S']?2:0)|(g_keyState['D']?1:0);
            wsprintfA(b, "fpsinput: MOVE x=%.0f y=%.0f keys=%d moves=%d\n",
                      g_px, g_py, wk, g_moves);
            dbg(b);
        }
    }
    /* projectiles */
    for (int i = 0; i < MAXP; i++) {
        if (!g_proj[i].live) continue;
        g_proj[i].x += g_proj[i].vx;
        g_proj[i].y += g_proj[i].vy;
        if (g_proj[i].x < 0 || g_proj[i].x > W ||
            g_proj[i].y < 0 || g_proj[i].y > H) {
            g_proj[i].live = 0; continue;
        }
        /* hit test vs targets */
        for (int t = 0; t < MAXT; t++) {
            if (!g_tgt[t].live) continue;
            float ddx = g_proj[i].x - g_tgt[t].x;
            float ddy = g_proj[i].y - g_tgt[t].y;
            if (ddx*ddx + ddy*ddy < 400.0f) {
                g_proj[i].live = 0; g_tgt[t].live = 0;
                g_hits++; g_score += 100;
                wsprintfA(b, "fpsinput: HIT n=%d score=%d\n", g_hits, g_score);
                dbg(b);
                /* respawn target */
                g_tgt[t].x = 60.0f + (g_hits * 97) % (W-120);
                g_tgt[t].y = 60.0f + (g_hits * 61) % (H-200);
                g_tgt[t].live = 1;
                break;
            }
        }
    }
}

static void render(IDirect3DDevice9* dev) {
    V v[1024]; int n = 0;
    /* background */
    quad(v+n, 0, 0, W, H, 0xff101018); n += 6;
    /* targets (red squares) */
    for (int t = 0; t < MAXT; t++) {
        if (g_tgt[t].live) { quad(v+n, g_tgt[t].x-10, g_tgt[t].y-10, 20, 20, 0xffff0000); n += 6; }
    }
    /* projectiles (yellow) */
    for (int i = 0; i < MAXP; i++) {
        if (g_proj[i].live) { quad(v+n, g_proj[i].x-3, g_proj[i].y-3, 6, 6, 0xffffff00); n += 6; }
    }
    /* player (green box) */
    quad(v+n, g_px-12, g_py-12, 24, 24, 0xff00ff00); n += 6;
    /* aim direction indicator (white line from player) */
    {
        float ax = g_px + cosf(g_yaw) * 40.0f;
        float ay = g_py + sinf(g_yaw) * 40.0f;
        /* thick line as two quads */
        float px = -(ay - g_py), py = (ax - g_px);
        float len = sqrtf(px*px + py*py);
        if (len > 0.01f) { px = px/len*2.0f; py = py/len*2.0f; }
        v[n].x=g_px-px; v[n].y=g_py-py; v[n].z=0.5f; v[n].rhw=1.0f; v[n].color=0xffffffff; n++;
        v[n].x=g_px+px; v[n].y=g_py+py; v[n].z=0.5f; v[n].rhw=1.0f; v[n].color=0xffffffff; n++;
        v[n].x=ax-px;   v[n].y=ay-py;   v[n].z=0.5f; v[n].rhw=1.0f; v[n].color=0xffffffff; n++;
        v[n].x=g_px-px; v[n].y=g_py-py; v[n].z=0.5f; v[n].rhw=1.0f; v[n].color=0xffffffff; n++;
        v[n].x=ax+px;   v[n].y=ay+py;   v[n].z=0.5f; v[n].rhw=1.0f; v[n].color=0xffffffff; n++;
        v[n].x=ax-px;   v[n].y=ay-py;   v[n].z=0.5f; v[n].rhw=1.0f; v[n].color=0xffffffff; n++;
    }
    IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xff101018, 1.0f, 0);
    IDirect3DDevice9_BeginScene(dev);
    IDirect3DDevice9_SetFVF(dev, FVF);
    IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, n/3, v, sizeof(V));
    IDirect3DDevice9_EndScene(dev);
    IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int cs) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc; wc.hInstance = hi;
    wc.lpszClassName = "fpsinput"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("fpsinput", "fpsinput",
        WS_OVERLAPPEDWINDOW, 100, 100, W+16, H+39, NULL, NULL, hi, NULL);
    if (!hwnd) { dbg("fpsinput: RESULT 1 no window\n"); return 1; }

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { dbg("fpsinput: RESULT 1 no d3d9\n"); return 1; }
    D3DPRESENT_PARAMETERS pp = {0};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev = NULL;
    if (FAILED(IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
            hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev))) {
        dbg("fpsinput: RESULT 1 no device\n"); return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    spawnTargets();
    dbg("fpsinput: STARTED\n");

    MSG msg;
    char b[192];
    DWORD lastTick = GetTickCount();
    while (g_frame < FRAMES) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        step();
        render(dev);
        g_frame++;
        if (g_frame % 600 == 0) {
            wsprintfA(b, "fpsinput: frame %d px=%.0f py=%.0f yaw=%.3f shots=%d score=%d\n",
                      g_frame, g_px, g_py, g_yaw, g_shots, g_score);
            dbg(b);
        }
        /* ~60fps pacing */
        DWORD now = GetTickCount();
        if (now - lastTick < 16) Sleep(16 - (now - lastTick));
        lastTick = GetTickCount();
    }
done:
    wsprintfA(b, "fpsinput: RESULT 0 frames=%d shots=%d hits=%d score=%d looks=%d moves=%d\n",
              g_frame, g_shots, g_hits, g_score, g_looks, g_moves);
    dbg(b);
    return 0;
}
