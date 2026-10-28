/* sweep12.c -- D3DCompile compute-shader form sweep (diagnostic).
 * Tries a matrix of small compute shader sources/targets and prints one
 * verdict line per attempt: "sweep12: TRY <name> OK" or
 * "sweep12: TRY <name> FAIL <first line of the error blob>".
 * Purpose: find which HLSL compute form the in-guest d3dcompiler_47
 * accepts, after cs12's cs_5_0 RWStructuredBuffer form failed at 1:1
 * with E5000 (unexpected NEW_IDENTIFIER).
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *PFN_D3D_COMPILE)(const void*, SIZE_T, const char*, const void*, void*,
        const char*, const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);
static PFN_D3D_COMPILE pD3DCompile = NULL;

static void dbg(const char* s) { fprintf(stderr, "%s", s); fflush(stderr); }

static void try_compile(const char* name, const char* src, const char* target)
{
    char b[1024];
    ID3DBlob* code = NULL; ID3DBlob* err = NULL;
    HRESULT hr = pD3DCompile(src, strlen(src), NULL, NULL, NULL, "main", target, 0, 0, &code, &err);
    if (SUCCEEDED(hr) && code) {
        wsprintfA(b, "sweep12: TRY %s OK (target=%s, %lu bytes)\n", name, target,
                  (unsigned long)ID3D10Blob_GetBufferSize(code));
        ID3D10Blob_Release(code);
    } else {
        const char* e = err ? (const char*)ID3D10Blob_GetBufferPointer(err) : "(no error blob)";
        char first[300]; int i = 0;
        while (e[i] && e[i] != '\n' && e[i] != '\r' && i < 290) { first[i] = e[i]; i++; }
        first[i] = 0;
        wsprintfA(b, "sweep12: TRY %s FAIL (target=%s) hr=0x%08X err=%s\n", name, target, (unsigned)hr, first);
    }
    if (err) ID3D10Blob_Release(err);
    dbg(b);
}

int main(void)
{
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!compiler) { dbg("sweep12: FAIL LoadLibrary(d3dcompiler_47.dll)\n"); dbg("sweep12: RESULT 1\n"); return 1; }
    pD3DCompile = (PFN_D3D_COMPILE)GetProcAddress(compiler, "D3DCompile");
    if (!pD3DCompile) { dbg("sweep12: FAIL GetProcAddress(D3DCompile)\n"); dbg("sweep12: RESULT 1\n"); return 1; }

    /* Control: the graphics form every other probe uses successfully. */
    try_compile("ctrl-ps-rwt",
        "RWTexture2D<float4> tex : register(u0);\n"
        "float4 main(float4 pos : SV_Position) : SV_Target { tex[uint2(pos.xy)] = float4(1,0,0,1); return float4(1,0,0,1); }\n",
        "ps_5_0");
    /* The cs12 BUF form that failed. */
    try_compile("cs-buf-reg",
        "RWStructuredBuffer<uint> buf : register(u0);\n"
        "[numthreads(64,1,1)] void main(uint3 dtid : SV_DispatchThreadID)\n"
        "{ buf[dtid.x] = dtid.x * 3u + 7u; }\n",
        "cs_5_0");
    /* Same without the register annotation. */
    try_compile("cs-buf-noreg",
        "RWStructuredBuffer<uint> buf;\n"
        "[numthreads(64,1,1)] void main(uint3 dtid : SV_DispatchThreadID)\n"
        "{ buf[dtid.x] = dtid.x * 3u + 7u; }\n",
        "cs_5_0");
    /* Same with cs_5_1. */
    try_compile("cs-buf-51",
        "RWStructuredBuffer<uint> buf : register(u0);\n"
        "[numthreads(64,1,1)] void main(uint3 dtid : SV_DispatchThreadID)\n"
        "{ buf[dtid.x] = dtid.x * 3u + 7u; }\n",
        "cs_5_1");
    /* Minimal: no semantic parameter at all. */
    try_compile("cs-min",
        "RWStructuredBuffer<uint> buf : register(u0);\n"
        "[numthreads(1,1,1)] void main() { buf[0] = 7u; }\n",
        "cs_5_0");
    /* SV_GroupThreadID instead of SV_DispatchThreadID. */
    try_compile("cs-gtid",
        "RWStructuredBuffer<uint> buf : register(u0);\n"
        "[numthreads(64,1,1)] void main(uint3 dtid : SV_GroupThreadID)\n"
        "{ buf[dtid.x] = dtid.x; }\n",
        "cs_5_0");
    /* RWTexture2D compute write. */
    try_compile("cs-tex",
        "RWTexture2D<float4> tex : register(u0);\n"
        "[numthreads(4,4,1)] void main(uint3 dtid : SV_DispatchThreadID)\n"
        "{ tex[dtid.xy] = float4(1,0,0,1); }\n",
        "cs_5_0");
    /* Byte-address buffer write. */
    try_compile("cs-bab",
        "RWByteAddressBuffer buf : register(u0);\n"
        "[numthreads(64,1,1)] void main(uint3 dtid : SV_DispatchThreadID)\n"
        "{ buf.Store(dtid.x * 4, dtid.x); }\n",
        "cs_5_0");
    /* Structured read. */
    try_compile("cs-srv",
        "StructuredBuffer<uint> inp : register(t0);\n"
        "RWStructuredBuffer<uint> outp : register(u0);\n"
        "[numthreads(64,1,1)] void main(uint3 dtid : SV_DispatchThreadID)\n"
        "{ outp[dtid.x] = inp[dtid.x] * 2u; }\n",
        "cs_5_0");
    /* numthreads after the signature line break / entry named differently. */
    try_compile("cs-alt",
        "RWStructuredBuffer<uint> buf : register(u0);\n"
        "[numthreads(64, 1, 1)]\n"
        "void main(uint3 dtid : SV_DispatchThreadID) { buf[dtid.x] = 1u; }\n",
        "cs_5_0");
    dbg("sweep12: RESULT 0\n");
    return 0;
}
