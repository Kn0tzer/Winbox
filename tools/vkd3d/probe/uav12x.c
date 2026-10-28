/*
 * uav12x.c -- diagnostic bisect for the uav12 UINT EXEC PSO wall
 * (lane-uintpso, 2026-10-28). NOT a verdict probe: RESULT is always 0;
 * the evidence is in the per-case lines.
 *
 * uav12's R32_UINT / R8_UINT EXEC cases fail CreateGraphicsPipelineState
 * with E_INVALIDARG while the float formats pass. The PSO desc, root
 * signature, and VS are identical across cases; only the PS blob varies.
 * The bridge log of a failing run shows vkCreatePipelineLayout x2 and
 * the VS module, but NO PS module -- vkd3d-proton bails before creating
 * it. This probe varies ONE thing at a time:
 *   - uint vs uint4 vs int vs float UAV element type
 *   - load vs store-only vs declared-but-unused
 *   - float(v) arithmetic vs asfloat bitcast
 *   - UAV table present vs empty root signature
 *   - UAV (u1) vs SRV texture (t0) uint load
 * and dumps the DXBC hex of the float (passing) and uint (failing)
 * pixel shaders for offline dxbc-spirv analysis.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -o uav12x.exe uav12x.c -lole32
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
    dbg("uav12x: RESULT 0\n"); return 0; } while (0)

typedef HRESULT (WINAPI *PFN_D3D12_CREATE_DEVICE)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12_SERIALIZE_ROOT_SIG)(const D3D12_ROOT_SIGNATURE_DESC*,
        D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI *PFN_D3D_COMPILE)(const void*, SIZE_T, const char*, const void*, void*,
        const char*, const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);

static PFN_D3D_COMPILE pD3DCompile = NULL;
static PFN_D3D12_SERIALIZE_ROOT_SIG pSerialize = NULL;
static ID3D12Device* g_dev = NULL;
static ID3DBlob* g_vs = NULL;

static ID3DBlob* compile_shader(const char* src, const char* target, const char* what, int dumpHex)
{
    ID3DBlob* code = NULL; ID3DBlob* err = NULL;
    HRESULT hr = pD3DCompile(src, strlen(src), NULL, NULL, NULL, "main", target, 0, 0, &code, &err);
    if (FAILED(hr)) {
        char b[1024];
        wsprintfA(b, "uav12x: FAIL D3DCompile(%s, %s) hr=0x%08X err=%s\n", what, target, (unsigned)hr,
                  err ? (const char*)ID3D10Blob_GetBufferPointer(err) : "(none)");
        dbg(b);
        if (err) ID3D10Blob_Release(err);
        return NULL;
    }
    if (err) ID3D10Blob_Release(err);
    if (dumpHex && code) {
        const unsigned char* p = (const unsigned char*)ID3D10Blob_GetBufferPointer(code);
        SIZE_T n = ID3D10Blob_GetBufferSize(code), off = 0; int line = 0;
        char b[512];
        wsprintfA(b, "uav12x: DXBC %s size=%u\n", what, (unsigned)n); dbg(b);
        while (off < n) {
            SIZE_T chunk = n - off > 48 ? 48 : n - off;
            int pos = wsprintfA(b, "uav12x: DXBC %s %d ", what, line++);
            for (SIZE_T i = 0; i < chunk; i++) pos += wsprintfA(b + pos, "%02x", p[off + i]);
            wsprintfA(b + pos, "\n"); dbg(b);
            off += chunk;
        }
    }
    return code;
}

/* mode: 0 = UAV table @u1, 1 = empty root signature, 2 = SRV table @t0 */
static void try_pso(const char* name, const char* ps_src, int mode, int dumpHex)
{
    char b[512];
    ID3DBlob* ps = compile_shader(ps_src, "ps_5_0", name, dumpHex);
    if (!ps) { wsprintfA(b, "uav12x: CASE %-12s compile=FAIL\n", name); dbg(b); return; }

    D3D12_DESCRIPTOR_RANGE range = {0};
    range.NumDescriptors = 1; range.OffsetInDescriptorsFromTableStart = 0;
    if (mode == 0) { range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; range.BaseShaderRegister = 1; }
    if (mode == 2) { range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.BaseShaderRegister = 0; }
    D3D12_ROOT_PARAMETER param = {0};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    if (mode != 1) { rsd.NumParameters = 1; rsd.pParameters = &param; }
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* sigBlob = NULL; ID3DBlob* sigErr = NULL;
    HRESULT hr = pSerialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &sigErr);
    if (FAILED(hr)) { wsprintfA(b, "uav12x: CASE %-12s serialize=FAIL hr=0x%08X\n", name, (unsigned)hr); dbg(b); ID3D10Blob_Release(ps); return; }
    ID3D12RootSignature* rootsig = NULL;
    hr = ID3D12Device_CreateRootSignature(g_dev, 0, ID3D10Blob_GetBufferPointer(sigBlob),
                                          ID3D10Blob_GetBufferSize(sigBlob), &IID_ID3D12RootSignature, (void**)&rootsig);
    if (FAILED(hr)) { wsprintfA(b, "uav12x: CASE %-12s rootsig=FAIL hr=0x%08X\n", name, (unsigned)hr); dbg(b); goto out; }

    static const D3D12_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {0};
    pso.pRootSignature = rootsig;
    pso.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(g_vs);
    pso.VS.BytecodeLength = ID3D10Blob_GetBufferSize(g_vs);
    pso.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps);
    pso.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
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
    hr = ID3D12Device_CreateGraphicsPipelineState(g_dev, &pso, &IID_ID3D12PipelineState, (void**)&pipe);
    wsprintfA(b, "uav12x: CASE %-12s compile=OK pso_hr=0x%08X\n", name, (unsigned)hr); dbg(b);
    if (pipe) ID3D12PipelineState_Release(pipe);
    ID3D12RootSignature_Release(rootsig);
out:
    ID3D10Blob_Release(sigBlob); if (sigErr) ID3D10Blob_Release(sigErr);
    ID3D10Blob_Release(ps);
}

static const char PS_F4[] =
    "RWTexture2D<float4> uav : register(u1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { return uav[int2(pos.xy)]; }\n";
static const char PS_U1[] =
    "RWTexture2D<uint> uav : register(u1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { uint v = uav[int2(pos.xy)]; return float4(float(v) * (1.0/255.0), 0.0, 0.0, 1.0); }\n";
static const char PS_U1_ASFLOAT[] =
    "RWTexture2D<uint> uav : register(u1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { uint v = uav[int2(pos.xy)]; return float4(asfloat(v), 0.0, 0.0, 1.0); }\n";
static const char PS_U1_DECL[] =
    "RWTexture2D<uint> uav : register(u1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { return float4(0.25, 0.5, 0.75, 1.0); }\n";
static const char PS_U1_STORE[] =
    "RWTexture2D<uint> uav : register(u1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { uav[int2(pos.xy)] = 7u; return float4(0.25, 0.5, 0.75, 1.0); }\n";
static const char PS_U4[] =
    "RWTexture2D<uint4> uav : register(u1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { uint4 v = uav[int2(pos.xy)]; return asfloat(v); }\n";
static const char PS_S1[] =
    "RWTexture2D<int> uav : register(u1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { int v = uav[int2(pos.xy)]; return float4(float(v) * (1.0/255.0), 0.0, 0.0, 1.0); }\n";
static const char PS_F1[] =
    "RWTexture2D<float> uav : register(u1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { float v = uav[int2(pos.xy)]; return float4(v, v, v, 1.0); }\n";
static const char PS_U1_SRV[] =
    "Texture2D<uint> tex : register(t0);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { uint v = tex.Load(int3(int2(pos.xy), 0)); return float4(float(v) * (1.0/255.0), 0.0, 0.0, 1.0); }\n";

int main(void)
{
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    if (!d3d12) FAIL("uav12x: FAIL LoadLibrary(d3d12.dll)\n");
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!compiler) FAIL("uav12x: FAIL LoadLibrary(d3dcompiler_47.dll)\n");
    pD3DCompile = (PFN_D3D_COMPILE)GetProcAddress(compiler, "D3DCompile");
    PFN_D3D12_CREATE_DEVICE pCreate =
        (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12, "D3D12CreateDevice");
    pSerialize = (PFN_D3D12_SERIALIZE_ROOT_SIG)GetProcAddress(d3d12, "D3D12SerializeRootSignature");
    if (!pD3DCompile || !pCreate || !pSerialize) FAIL("uav12x: FAIL GetProcAddress\n");

    HRESULT hr = pCreate(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&g_dev);
    if (FAILED(hr)) FAIL("uav12x: FAIL D3D12CreateDevice hr=0x%08X\n", (unsigned)hr);
    dbg("uav12x: D3D12CreateDevice OK\n");

    static const char* VS_SRC =
        "struct VSIn { float3 pos : POSITION; float4 col : COLOR; };\n"
        "struct VSOut { float4 pos : SV_Position; float4 col : COLOR; };\n"
        "VSOut main(VSIn i) { VSOut o; o.pos = float4(i.pos, 1.0); o.col = i.col; return o; }\n";
    g_vs = compile_shader(VS_SRC, "vs_5_0", "vs", 0);
    if (!g_vs) FAIL("uav12x: FAIL VS compile\n");

    try_pso("f4-uav",     PS_F4,       0, 1);
    try_pso("u1-uav",     PS_U1,       0, 1);
    try_pso("u1-empty",   PS_U1,       1, 0);
    try_pso("u1-decl",    PS_U1_DECL,  0, 0);
    try_pso("u1-asfloat", PS_U1_ASFLOAT, 0, 0);
    try_pso("u1-store",   PS_U1_STORE, 0, 0);
    try_pso("u4-uav",     PS_U4,       0, 0);
    try_pso("s1-uav",     PS_S1,       0, 0);
    try_pso("f1-uav",     PS_F1,       0, 0);
    try_pso("u1-srv",     PS_U1_SRV,   2, 0);

    ID3D10Blob_Release(g_vs);
    ID3D12Device_Release(g_dev);
    dbg("uav12x: RESULT 0\n");
    return 0;
}
