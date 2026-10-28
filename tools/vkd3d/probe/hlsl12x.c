/*
 * hlsl12x.c -- diagnostic sweep for the root12 srv/uav probe debt
 * (lane-uintpso, 2026-10-28). Compile-only: for each candidate HLSL
 * buffer declaration form, D3DCompile a trivial ps_5_0 and print OK
 * (+ blob size) or FAIL (+ the compiler's own error text).
 *
 * Background: root12's srv case uses Buffer<uint> and its uav case
 * RWBuffer<uint>; Wine 10's builtin d3dcompiler_47 (vkd3d-shader HLSL
 * front end) rejects both at 1:1 ("unexpected KW_BUFFER" /
 * "unexpected NEW_IDENTIFIER"), and the header comment records the
 * same for ByteAddressBuffer / RWByteAddressBuffer. A root SRV/UAV is
 * a raw GPU VA regardless of the shader's declared buffer flavor, so
 * ANY buffer flavor this compiler accepts (typed / structured / raw)
 * exercises the same vkd3d root-descriptor code path. This sweep
 * finds which flavors parse.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -o hlsl12x.exe hlsl12x.c -lole32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <stdio.h>
#include <string.h>

static void dbg(const char* s) { fprintf(stderr, "%s", s); fflush(stderr); }

typedef HRESULT (WINAPI *PFN_D3D_COMPILE)(const void*, SIZE_T, const char*, const void*, void*,
        const char*, const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);
static PFN_D3D_COMPILE pD3DCompile = NULL;

static void try_form(const char* name, const char* src)
{
    char b[2048];
    ID3DBlob* code = NULL; ID3DBlob* err = NULL;
    HRESULT hr = pD3DCompile(src, strlen(src), NULL, NULL, NULL, "main", "ps_5_0", 0, 0, &code, &err);
    if (SUCCEEDED(hr) && code) {
        wsprintfA(b, "hlsl12x: FORM %-14s OK size=%u\n", name, (unsigned)ID3D10Blob_GetBufferSize(code));
        dbg(b);
        ID3D10Blob_Release(code);
    } else {
        char flat[1024]; flat[0] = 0;
        if (err) {
            const char* e = (const char*)ID3D10Blob_GetBufferPointer(err);
            SIZE_T n = strlen(e); if (n > 900) n = 900;
            SIZE_T j = 0;
            for (SIZE_T i = 0; i < n && j < 1000; i++)
                flat[j++] = (e[i] == '\n' || e[i] == '\r') ? '|' : e[i];
            flat[j] = 0;
        }
        wsprintfA(b, "hlsl12x: FORM %-14s FAIL hr=0x%08X err=%s\n", name, (unsigned)hr, flat);
        dbg(b);
    }
    if (err) ID3D10Blob_Release(err);
}

#define PS_TAIL_SRV "struct PSIn { float4 pos : SV_Position; float4 col : COLOR; };\n"

int main(void)
{
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!compiler) { dbg("hlsl12x: FAIL LoadLibrary(d3dcompiler_47.dll)\n"); dbg("hlsl12x: RESULT 0\n"); return 0; }
    pD3DCompile = (PFN_D3D_COMPILE)GetProcAddress(compiler, "D3DCompile");
    if (!pD3DCompile) { dbg("hlsl12x: FAIL GetProcAddress(D3DCompile)\n"); dbg("hlsl12x: RESULT 0\n"); return 0; }

    /* Controls: known to compile in-guest today. */
    try_form("ctl-cbv",
        "cbuffer RootCb : register(b0) { float4 gMul; float4 gAdd; };\n"
        PS_TAIL_SRV
        "float4 main(PSIn i) : SV_Target { return i.col * gMul + gAdd; }\n");
    try_form("ctl-rwtex",
        "RWTexture2D<float4> uav : register(u1);\n"
        "float4 main(float4 pos : SV_Position) : SV_Target { return uav[int2(pos.xy)]; }\n");

    /* SRV buffer flavors (root12 srv case shape). */
    try_form("buf-idx",
        "Buffer<uint> g : register(t0);\n" PS_TAIL_SRV
        "float4 main(PSIn i) : SV_Target { return float4(asfloat(g[0]), asfloat(g[1]), asfloat(g[2]), 1.0); }\n");
    try_form("buf-load",
        "Buffer<uint> g : register(t0);\n" PS_TAIL_SRV
        "float4 main(PSIn i) : SV_Target { return float4(asfloat(g.Load(0)), asfloat(g.Load(1)), asfloat(g.Load(2)), 1.0); }\n");
    try_form("buff4-idx",
        "Buffer<float4> g : register(t0);\n" PS_TAIL_SRV
        "float4 main(PSIn i) : SV_Target { return g[0]; }\n");
    try_form("sbuf-idx",
        "StructuredBuffer<uint> g : register(t0);\n" PS_TAIL_SRV
        "float4 main(PSIn i) : SV_Target { return float4(asfloat(g[0]), asfloat(g[1]), asfloat(g[2]), 1.0); }\n");
    try_form("sbuf-load",
        "StructuredBuffer<uint> g : register(t0);\n" PS_TAIL_SRV
        "float4 main(PSIn i) : SV_Target { return float4(asfloat(g.Load(0)), asfloat(g.Load(1)), asfloat(g.Load(2)), 1.0); }\n");
    try_form("bab-load",
        "ByteAddressBuffer g : register(t0);\n" PS_TAIL_SRV
        "float4 main(PSIn i) : SV_Target { return float4(asfloat(g.Load(0)), asfloat(g.Load(4)), asfloat(g.Load(8)), 1.0); }\n");

    /* UAV buffer flavors (root12 uav case shape). */
    try_form("rwbuf-idx",
        "RWBuffer<uint> g : register(u0);\n" PS_TAIL_SRV
        "float4 main(PSIn i) : SV_Target { uint idx = (uint)i.pos.x + 4u * (uint)i.pos.y; g[idx] = 0xA5000000u + idx; return float4(0.25, 0.5, 0.75, 1.0); }\n");
    try_form("rwsbuf-idx",
        "RWStructuredBuffer<uint> g : register(u0);\n" PS_TAIL_SRV
        "float4 main(PSIn i) : SV_Target { uint idx = (uint)i.pos.x + 4u * (uint)i.pos.y; g[idx] = 0xA5000000u + idx; return float4(0.25, 0.5, 0.75, 1.0); }\n");
    try_form("rwbab-store",
        "RWByteAddressBuffer g : register(u0);\n" PS_TAIL_SRV
        "float4 main(PSIn i) : SV_Target { uint idx = (uint)i.pos.x + 4u * (uint)i.pos.y; g.Store(4u * idx, 0xA5000000u + idx); return float4(0.25, 0.5, 0.75, 1.0); }\n");

    dbg("hlsl12x: RESULT 0\n");
    return 0;
}
