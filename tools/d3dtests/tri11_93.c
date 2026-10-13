// tri11_93.c — D3D11 Feature Level 9_3 verification test (64-bit).
// 1. D3D11CreateDevice requesting {9_3, 9_2, 9_1} explicitly; log resulting FL.
// 2. Render a colored triangle to an offscreen render target (no swapchain).
// 3. Copy to staging texture, map, count lit pixels.
// Reports: tri1193: FL=0x... / tri1193: LIT=<n> / tri1193: RESULT 0|1
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>

static const char* vs_src =
    "struct VSIn { float3 pos : POSITION; float4 col : COLOR; };\n"
    "struct PSIn { float4 pos : SV_Position; float4 col : COLOR; };\n"
    "PSIn main(VSIn i) { PSIn o; o.pos = float4(i.pos, 1.0); o.col = i.col; return o; }\n";
static const char* ps_src =
    "struct PSIn { float4 pos : SV_Position; float4 col : COLOR; };\n"
    "float4 main(PSIn i) : SV_Target { return i.col; }\n";

int main(void) {
    HRESULT hr;
    ID3D11Device* dev = NULL;
    ID3D11DeviceContext* ctx = NULL;
    D3D_FEATURE_LEVEL fl_out = 0;
    D3D_FEATURE_LEVEL fl_req[] = {
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };
    fprintf(stderr, "tri1193: start\n");
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                           fl_req, 3, D3D11_SDK_VERSION,
                           &dev, &fl_out, &ctx);
    if (FAILED(hr)) {
        fprintf(stderr, "tri1193: FAIL D3D11CreateDevice hr=0x%08X\n", (unsigned)hr);
        fprintf(stderr, "tri1193: RESULT 1\n");
        return 1;
    }
    fprintf(stderr, "tri1193: D3D11CreateDevice OK FL=0x%x\n", (unsigned)fl_out);

    // Compile shaders (9_3 profiles)
    ID3DBlob* vsBlob = NULL; ID3DBlob* psBlob = NULL; ID3DBlob* errBlob = NULL;
    hr = D3DCompile(vs_src, strlen(vs_src), NULL, NULL, NULL, "main",
                    "vs_4_0_level_9_3", 0, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        fprintf(stderr, "tri1193: FAIL VS compile hr=0x%08X %s\n", (unsigned)hr,
                errBlob ? (char*)errBlob->lpVtbl->GetBufferPointer(errBlob) : "");
        fprintf(stderr, "tri1193: RESULT 1\n");
        return 1;
    }
    hr = D3DCompile(ps_src, strlen(ps_src), NULL, NULL, NULL, "main",
                    "ps_4_0_level_9_3", 0, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
        fprintf(stderr, "tri1193: FAIL PS compile hr=0x%08X\n", (unsigned)hr);
        fprintf(stderr, "tri1193: RESULT 1\n");
        return 1;
    }
    fprintf(stderr, "tri1193: shaders compiled\n");

    ID3D11VertexShader* vs = NULL; ID3D11PixelShader* ps = NULL;
    ID3D11Device_CreateVertexShader(dev, vsBlob->lpVtbl->GetBufferPointer(vsBlob),
        vsBlob->lpVtbl->GetBufferSize(vsBlob), NULL, &vs);
    ID3D11Device_CreatePixelShader(dev, psBlob->lpVtbl->GetBufferPointer(psBlob),
        psBlob->lpVtbl->GetBufferSize(psBlob), NULL, &ps);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D11InputLayout* il = NULL;
    ID3D11Device_CreateInputLayout(dev, layout, 2,
        vsBlob->lpVtbl->GetBufferPointer(vsBlob),
        vsBlob->lpVtbl->GetBufferSize(vsBlob), &il);

    struct V { float x, y, z, r, g, b, a; } verts[] = {
        {  0.0f,  0.5f, 0.5f,  1.0f, 0.0f, 0.0f, 1.0f },
        {  0.5f, -0.5f, 0.5f,  0.0f, 1.0f, 0.0f, 1.0f },
        { -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, 1.0f, 1.0f },
    };
    D3D11_BUFFER_DESC bd = { 0 };
    bd.ByteWidth = sizeof(verts); bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = { 0 }; sd.pSysMem = verts;
    ID3D11Buffer* vb = NULL;
    ID3D11Device_CreateBuffer(dev, &bd, &sd, &vb);

    // Offscreen render target 256x256
    D3D11_TEXTURE2D_DESC td = { 0 };
    td.Width = 256; td.Height = 256; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* rt = NULL;
    hr = ID3D11Device_CreateTexture2D(dev, &td, NULL, &rt);
    if (FAILED(hr)) { fprintf(stderr, "tri1193: FAIL RT hr=0x%08X\n", (unsigned)hr);
        fprintf(stderr, "tri1193: RESULT 1\n"); return 1; }
    ID3D11RenderTargetView* rtv = NULL;
    ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource*)rt, NULL, &rtv);

    // Staging for readback
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = NULL;
    ID3D11Device_CreateTexture2D(dev, &td, NULL, &staging);

    D3D11_VIEWPORT vp = { 0, 0, 256, 256, 0.0f, 1.0f };
    float clear[4] = { 0.05f, 0.05f, 0.08f, 1.0f };
    UINT stride = sizeof(struct V), offset = 0;

    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, clear);
    ID3D11DeviceContext_IASetInputLayout(ctx, il);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 3, 0);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource*)staging, (ID3D11Resource*)rt);

    D3D11_MAPPED_SUBRESOURCE m = { 0 };
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource*)staging, 0, D3D11_MAP_READ, 0, &m);
    int lit = 0;
    if (SUCCEEDED(hr)) {
        for (int y = 0; y < 256; y++) {
            unsigned char* row = (unsigned char*)m.pData + y * m.RowPitch;
            for (int x = 0; x < 256; x++) {
                unsigned char r = row[x*4], g = row[x*4+1], b = row[x*4+2];
                // background is ~(13,13,20); triangle pixels are bright primary colors
                if (r > 40 || g > 40 || b > 40) lit++;
            }
        }
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource*)staging, 0);
    }
    fprintf(stderr, "tri1193: LIT=%d\n", lit);
    int ok = (lit > 1000);
    fprintf(stderr, "tri1193: RESULT %d\n", ok ? 0 : 1);
    return ok ? 0 : 1;
}
