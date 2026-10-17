/*
 * scene10.cpp — D3D10 scene probe (28 draw calls/frame), mirrors scene9.cpp.
 * Renders a "game-like" 2D scene: parallax background (8 quads), mid-ground
 * sprites (12 quads, alpha blending), foreground UI (8 quads).
 * 3 procedural textures, per-frame dynamic vertex buffer updates.
 *
 * Build: x86_64-w64-mingw32-g++ -O2 -o scene10.exe scene10.cpp -ld3d10 -ldxgi -ld3dcompiler -lgdi32 -luser32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10.h>
#include <d3dcompiler.h>
#include <stdio.h>

#define FRAMES 120
#define W 640
#define H 480
#define QUADS_BG 8
#define QUADS_MID 12
#define QUADS_FG 8
#define TEXW 64
#define TEXH 64

#define FAIL(...) do { printf(__VA_ARGS__); printf("scene10: RESULT 1\n"); fflush(stdout); return 1; } while (0)

typedef struct { float x, y, z; float r, g, b, a; float u, v; } VERTEX;

static const char* VS_SRC =
"struct VS_IN { float3 pos : POSITION; float4 col : COLOR; float2 tex : TEXCOORD; };\n"
"struct VS_OUT { float4 pos : SV_POSITION; float4 col : COLOR; float2 tex : TEXCOORD; };\n"
"VS_OUT main(VS_IN i) { VS_OUT o; o.pos = float4(i.pos, 1.0); o.col = i.col; o.tex = i.tex; return o; }\n";
static const char* PS_SRC =
"Texture2D tex : register(t0);\n"
"SamplerState samp : register(s0);\n"
"float4 main(float4 pos : SV_POSITION, float4 col : COLOR, float2 t : TEXCOORD) : SV_TARGET {\n"
"  return tex.Sample(samp, t) * col;\n"
"}\n";

static void fill_checker(DWORD* px) {
    for (int y = 0; y < TEXH; y++) for (int x = 0; x < TEXW; x++) {
        int c = ((x / 8) + (y / 8)) & 1; BYTE v = c ? 220 : 40;
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
        BYTE a = (BYTE)(180 + (x * 75 / TEXW));
        px[y * TEXW + x] = ((DWORD)a << 24) | (r << 16) | (g << 8) | b;
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE || m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

static ID3D10ShaderResourceView* make_tex(ID3D10Device* dev, void (*fill)(DWORD*)) {
    D3D10_TEXTURE2D_DESC td; memset(&td, 0, sizeof(td));
    td.Width = TEXW; td.Height = TEXH; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D10_USAGE_DEFAULT; td.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    DWORD px[TEXW * TEXH]; fill(px);
    D3D10_SUBRESOURCE_DATA init; memset(&init, 0, sizeof(init));
    init.pSysMem = px; init.SysMemPitch = TEXW * 4;
    ID3D10Texture2D* t = NULL;
    if (FAILED(dev->CreateTexture2D( &td, &init, &t))) return NULL;
    ID3D10ShaderResourceView* srv = NULL;
    if (FAILED(dev->CreateShaderResourceView( (ID3D10Resource*)t, NULL, &srv))) {
        t->Release(); return NULL;
    }
    t->Release();
    return srv;
}

static ID3D10VertexShader* compile_vs(ID3D10Device* dev, ID3D10InputLayout** layout) {
    ID3DBlob* code = NULL; ID3DBlob* err = NULL;
    HRESULT hr = D3DCompile(VS_SRC, strlen(VS_SRC), NULL, NULL, NULL, "main", "vs_4_0",
        D3D10_SHADER_ENABLE_STRICTNESS, 0, &code, &err);
    if (FAILED(hr)) {
        if (err) printf("VS compile err: %s\n", (char*)err->GetBufferPointer());
        return NULL;
    }
    ID3D10VertexShader* vs = NULL;
    if (FAILED(dev->CreateVertexShader( code->GetBufferPointer(),
            code->GetBufferSize(), &vs))) { code->Release(); return NULL; }
    D3D10_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(dev->CreateInputLayout( elems, 3, code->GetBufferPointer(),
            code->GetBufferSize(), layout))) {
        code->Release(); vs->Release(); return NULL;
    }
    code->Release();
    return vs;
}

static ID3D10PixelShader* compile_ps(ID3D10Device* dev) {
    ID3DBlob* code = NULL; ID3DBlob* err = NULL;
    HRESULT hr = D3DCompile(PS_SRC, strlen(PS_SRC), NULL, NULL, NULL, "main", "ps_4_0",
        D3D10_SHADER_ENABLE_STRICTNESS, 0, &code, &err);
    if (FAILED(hr)) {
        if (err) printf("PS compile err: %s\n", (char*)err->GetBufferPointer());
        return NULL;
    }
    ID3D10PixelShader* ps = NULL;
    if (FAILED(dev->CreatePixelShader( code->GetBufferPointer(),
            code->GetBufferSize(), &ps))) { code->Release(); return NULL; }
    code->Release();
    return ps;
}

static void draw_quad(ID3D10Device* dev, ID3D10Buffer* vb,
        float qx, float qy, float qw, float qh, float r, float g, float b, float a) {
    /* NDC coords: x in [-1,1], y in [-1,1] (y down -> flip) */
    float x0 = (qx / W) * 2.0f - 1.0f, x1 = ((qx + qw) / W) * 2.0f - 1.0f;
    float y0 = 1.0f - (qy / H) * 2.0f, y1 = 1.0f - ((qy + qh) / H) * 2.0f;
    VERTEX v[4] = {
        {x0, y0, 0.5f, r, g, b, a, 0, 0},
        {x1, y0, 0.5f, r, g, b, a, 1, 0},
        {x0, y1, 0.5f, r, g, b, a, 0, 1},
        {x1, y1, 0.5f, r, g, b, a, 1, 1},
    };
    void* p = NULL;
    if (FAILED(vb->Map( D3D10_MAP_WRITE_DISCARD, 0, &p))) return;
    memcpy(p, v, sizeof(v));
    vb->Unmap();
    UINT stride = sizeof(VERTEX), off = 0;
    dev->IASetVertexBuffers( 0, 1, &vb, &stride, &off);
    dev->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    dev->Draw( 4, 0);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hPrev; (void)cmd; (void)show;
    printf("scene10: start (28 draw calls/frame, 3 textures, alpha blending)\n"); fflush(stdout);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = "scene10";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("scene10", "scene10", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, W, H, 0, 0, hInst, 0);
    if (!hwnd) FAIL("CreateWindow failed\n");

    DXGI_SWAP_CHAIN_DESC scd; memset(&scd, 0, sizeof(scd));
    scd.BufferCount = 1; scd.BufferDesc.Width = W; scd.BufferDesc.Height = H;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd; scd.SampleDesc.Count = 1; scd.Windowed = TRUE;

    ID3D10Device* dev = NULL;
    IDXGISwapChain* swap = NULL;
    HRESULT hr = D3D10CreateDeviceAndSwapChain(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
        D3D10_SDK_VERSION, &scd, &swap, &dev);
    printf("scene10: D3D10CreateDeviceAndSwapChain hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
    if (FAILED(hr)) FAIL("D3D10 device creation failed hr=0x%08lx\n", (unsigned long)hr);

    ID3D10Texture2D* back = NULL;
    ID3D10RenderTargetView* rtv = NULL;
    if (FAILED(swap->GetBuffer(0, IID_ID3D10Texture2D, (void**)&back)))
        FAIL("GetBuffer failed\n");
    if (FAILED(dev->CreateRenderTargetView( (ID3D10Resource*)back, NULL, &rtv)))
        FAIL("CreateRTV failed\n");
    back->Release();
    dev->OMSetRenderTargets( 1, &rtv, NULL);

    D3D10_VIEWPORT vp; memset(&vp, 0, sizeof(vp)); vp.Width=W; vp.Height=H; vp.MaxDepth=1.0f;
    dev->RSSetViewports( 1, &vp);

    ID3D10InputLayout* layout = NULL;
    ID3D10VertexShader* vs = compile_vs(dev, &layout);
    if (!vs) FAIL("VS compile/create failed\n");
    ID3D10PixelShader* ps = compile_ps(dev);
    if (!ps) FAIL("PS compile/create failed\n");
    dev->VSSetShader( vs);
    dev->PSSetShader( ps);
    dev->IASetInputLayout( layout);

    ID3D10ShaderResourceView* texs[3];
    texs[0] = make_tex(dev, fill_checker);
    texs[1] = make_tex(dev, fill_gradient);
    texs[2] = make_tex(dev, fill_stripes);
    for (int i = 0; i < 3; i++) if (!texs[i]) FAIL("texture %d failed\n", i);

    D3D10_SAMPLER_DESC sd; memset(&sd, 0, sizeof(sd));
    sd.Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    ID3D10SamplerState* samp = NULL;
    if (FAILED(dev->CreateSamplerState( &sd, &samp))) FAIL("sampler failed\n");
    dev->PSSetSamplers( 0, 1, &samp);

    D3D10_BLEND_DESC bd; memset(&bd, 0, sizeof(bd));
    bd.BlendEnable[0] = TRUE;
    bd.SrcBlend = D3D10_BLEND_SRC_ALPHA; bd.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
    bd.BlendOp = D3D10_BLEND_OP_ADD;
    bd.SrcBlendAlpha = D3D10_BLEND_ONE; bd.DestBlendAlpha = D3D10_BLEND_ZERO;
    bd.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    bd.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
    ID3D10BlendState* blendOn = NULL;
    if (FAILED(dev->CreateBlendState( &bd, &blendOn))) FAIL("blend failed\n");
    FLOAT bf[4] = {0,0,0,0};

    D3D10_BUFFER_DESC vbd; memset(&vbd, 0, sizeof(vbd));
    vbd.ByteWidth = 4 * sizeof(VERTEX);
    vbd.Usage = D3D10_USAGE_DYNAMIC; vbd.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
    ID3D10Buffer* vb = NULL;
    if (FAILED(dev->CreateBuffer( &vbd, NULL, &vb))) FAIL("VB failed\n");

    float t = 0;
    for (int f = 0; f < FRAMES; f++) {
        float cc[4] = {0.1f, 0.1f, 0.3f, 1.0f};
        dev->ClearRenderTargetView( rtv, cc);
        /* background layer: 8 quads, no blending */
        dev->OMSetBlendState( NULL, bf, 0xffffffff);
        dev->PSSetShaderResources( 0, 1, &texs[0]);
        for (int i = 0; i < QUADS_BG; i++) {
            float qx = (i * 97 + (int)(t * 30)) % W, qy = (i * 53) % H;
            draw_quad(dev, vb, (float)qx, (float)qy, 90, 70, 1, 1, 1, 1);
        }
        /* sprite layer: 12 quads, alpha blending */
        dev->OMSetBlendState( blendOn, bf, 0xffffffff);
        dev->PSSetShaderResources( 0, 1, &texs[2]);
        for (int i = 0; i < QUADS_MID; i++) {
            float qx = (i * 61 + (int)(t * 60)) % W, qy = (i * 89) % H;
            draw_quad(dev, vb, (float)qx, (float)qy, 64, 64, 1, 0.8f, 0.6f, 0.85f);
        }
        /* UI layer: 8 quads, no blending */
        dev->OMSetBlendState( NULL, bf, 0xffffffff);
        dev->PSSetShaderResources( 0, 1, &texs[1]);
        for (int i = 0; i < QUADS_FG; i++) {
            float qx = (i * 83) % (W - 100), qy = H - 40 - (i % 2) * 44;
            draw_quad(dev, vb, (float)qx, (float)qy, 100, 36, 0.9f, 0.9f, 1, 1);
        }
        swap->Present( 0, 0);
        t += 1.0f / 60.0f;
        if ((f % 30) == 0) { printf("scene10: frame %d\n", f); fflush(stdout); }
    }
    printf("scene10: RESULT 0\n"); fflush(stdout);
    return 0;
}
