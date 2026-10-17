// tri10.c — Minimal D3D10 triangle test (64-bit).
// 1. D3D10CreateDevice (no swapchain, offscreen).
// 2. Render a colored triangle to an offscreen render target.
// 3. Copy to staging texture, map, count lit pixels.
// Reports: tri10: FL / tri10: LIT=<n> / tri10: RESULT 0|1
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10.h>
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
    ID3D10Device* dev = NULL;
    fprintf(stderr, "tri10: start\n");

    hr = D3D10CreateDevice(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
                           D3D10_SDK_VERSION, &dev);
    if (FAILED(hr)) {
        fprintf(stderr, "tri10: FAIL D3D10CreateDevice hr=0x%08X\n", (unsigned)hr);
        fprintf(stderr, "tri10: RESULT 1\n");
        return 1;
    }
    fprintf(stderr, "tri10: D3D10CreateDevice OK\n");

    // Compile shaders (simple vs_4_0/ps_4_0, no textures)
    ID3DBlob* vsBlob = NULL; ID3DBlob* psBlob = NULL; ID3DBlob* errBlob = NULL;
    hr = D3DCompile(vs_src, strlen(vs_src), NULL, NULL, NULL, "main",
                    "vs_4_0", 0, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        fprintf(stderr, "tri10: FAIL VS compile hr=0x%08X %s\n", (unsigned)hr,
                errBlob ? (char*)errBlob->lpVtbl->GetBufferPointer(errBlob) : "");
        fprintf(stderr, "tri10: RESULT 1\n");
        return 1;
    }
    hr = D3DCompile(ps_src, strlen(ps_src), NULL, NULL, NULL, "main",
                    "ps_4_0", 0, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
        fprintf(stderr, "tri10: FAIL PS compile hr=0x%08X\n", (unsigned)hr);
        fprintf(stderr, "tri10: RESULT 1\n");
        return 1;
    }
    fprintf(stderr, "tri10: shaders compiled\n");

    ID3D10VertexShader* vs = NULL; ID3D10PixelShader* ps = NULL;
    ID3D10Device_CreateVertexShader(dev, vsBlob->lpVtbl->GetBufferPointer(vsBlob),
        vsBlob->lpVtbl->GetBufferSize(vsBlob), &vs);
    ID3D10Device_CreatePixelShader(dev, psBlob->lpVtbl->GetBufferPointer(psBlob),
        psBlob->lpVtbl->GetBufferSize(psBlob), &ps);

    D3D10_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D10InputLayout* il = NULL;
    ID3D10Device_CreateInputLayout(dev, layout, 2,
        vsBlob->lpVtbl->GetBufferPointer(vsBlob), vsBlob->lpVtbl->GetBufferSize(vsBlob), &il);

    struct V { float x, y, z, r, g, b, a; } verts[] = {
        {  0.0f,  0.5f, 0.5f,  1.0f, 0.0f, 0.0f, 1.0f },
        {  0.5f, -0.5f, 0.5f,  0.0f, 1.0f, 0.0f, 1.0f },
        { -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, 1.0f, 1.0f },
    };
    D3D10_BUFFER_DESC bd; memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = sizeof(verts); bd.Usage = D3D10_USAGE_DEFAULT;
    bd.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    D3D10_SUBRESOURCE_DATA sd; memset(&sd, 0, sizeof(sd)); sd.pSysMem = verts;
    ID3D10Buffer* vb = NULL;
    ID3D10Device_CreateBuffer(dev, &bd, &sd, &vb);

    // Offscreen render target 256x256
    D3D10_TEXTURE2D_DESC td; memset(&td, 0, sizeof(td));
    td.Width = 256; td.Height = 256; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D10_USAGE_DEFAULT; td.BindFlags = D3D10_BIND_RENDER_TARGET;
    ID3D10Texture2D* rt = NULL;
    hr = ID3D10Device_CreateTexture2D(dev, &td, NULL, &rt);
    if (FAILED(hr)) {
        fprintf(stderr, "tri10: FAIL RT hr=0x%08X\n", (unsigned)hr);
        fprintf(stderr, "tri10: RESULT 1\n"); return 1;
    }
    ID3D10RenderTargetView* rtv = NULL;
    ID3D10Device_CreateRenderTargetView(dev, (ID3D10Resource*)rt, NULL, &rtv);

    // Staging for readback
    D3D10_TEXTURE2D_DESC sd2 = td;
    sd2.Usage = D3D10_USAGE_STAGING; sd2.BindFlags = 0;
    sd2.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    ID3D10Texture2D* staging = NULL;
    ID3D10Device_CreateTexture2D(dev, &sd2, NULL, &staging);

    // Render
    float clearColor[4] = { 0.0f, 0.0f, 0.2f, 1.0f };
    ID3D10Device_ClearRenderTargetView(dev, rtv, clearColor);
    ID3D10Device_IASetInputLayout(dev, il);
    ID3D10Device_IASetPrimitiveTopology(dev, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(struct V), offset = 0;
    ID3D10Device_IASetVertexBuffers(dev, 0, 1, &vb, &stride, &offset);
    ID3D10Device_VSSetShader(dev, vs);
    ID3D10Device_PSSetShader(dev, ps);
    D3D10_VIEWPORT vp = { 0, 0, 256, 256, 0.0f, 1.0f };
    ID3D10Device_RSSetViewports(dev, 1, &vp);
    ID3D10Device_OMSetRenderTargets(dev, 1, &rtv, NULL);
    ID3D10Device_Draw(dev, 3, 0);
    ID3D10Device_CopyResource(dev, (ID3D10Resource*)staging, (ID3D10Resource*)rt);

    // Map and count lit pixels
    D3D10_MAPPED_TEXTURE2D mapped;
    hr = ID3D10Texture2D_Map(staging, 0, D3D10_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        fprintf(stderr, "tri10: FAIL Map hr=0x%08X\n", (unsigned)hr);
        fprintf(stderr, "tri10: RESULT 1\n"); return 1;
    }
    int lit = 0;
    for (int y = 0; y < 256; y++) {
        unsigned char* row = (unsigned char*)mapped.pData + y * mapped.RowPitch;
        for (int x = 0; x < 256; x++) {
            unsigned char r = row[x*4+0], g = row[x*4+1], b = row[x*4+2];
            // Clear color is (0, 0, 0.2) = (0, 0, 51). Lit = not clear.
            if (r > 10 || g > 10 || b > 61) lit++;
        }
    }
    ID3D10Texture2D_Unmap(staging, 0);

    fprintf(stderr, "tri10: LIT=%d\n", lit);
    fprintf(stderr, "tri10: RESULT %d\n", lit > 1000 ? 0 : 1);
    return lit > 1000 ? 0 : 1;
}
