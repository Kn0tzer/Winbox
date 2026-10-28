/* sweep2.c -- follow-up compiler sweep after sweep12 found the guest
 * d3dcompiler_47 SM5 front-end lacks structured/byte-address buffer types
 * and rejects SV_DispatchThreadID, while RWTexture2D parses.
 * Questions:
 *  1. Is dxcompiler.dll present in the guest (SM6 path)?
 *  2. Which thread-id semantic does RWTexture2D compute accept?
 *  3. Does RWStructuredBuffer parse in a pixel shader (u1)?
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
        wsprintfA(b, "sweep2: TRY %s OK (target=%s, %lu bytes)\n", name, target,
                  (unsigned long)ID3D10Blob_GetBufferSize(code));
        ID3D10Blob_Release(code);
    } else {
        const char* e = err ? (const char*)ID3D10Blob_GetBufferPointer(err) : "(no error blob)";
        char first[300]; int i = 0;
        while (e[i] && e[i] != '\n' && e[i] != '\r' && i < 290) { first[i] = e[i]; i++; }
        first[i] = 0;
        wsprintfA(b, "sweep2: TRY %s FAIL (target=%s) hr=0x%08X err=%s\n", name, target, (unsigned)hr, first);
    }
    if (err) ID3D10Blob_Release(err);
    dbg(b);
}

int main(void)
{
    char b[512];
    HMODULE dxc = LoadLibraryA("dxcompiler.dll");
    wsprintfA(b, "sweep2: dxcompiler.dll %s\n", dxc ? "PRESENT" : "ABSENT");
    dbg(b);
    if (dxc) {
        void* p = (void*)GetProcAddress(dxc, "DxcCreateInstance");
        wsprintfA(b, "sweep2: DxcCreateInstance %s\n", p ? "PRESENT" : "ABSENT");
        dbg(b);
        HMODULE dxil = LoadLibraryA("dxil.dll");
        wsprintfA(b, "sweep2: dxil.dll %s\n", dxil ? "PRESENT" : "ABSENT");
        dbg(b);
    }
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!compiler) { dbg("sweep2: FAIL LoadLibrary(d3dcompiler_47.dll)\n"); dbg("sweep2: RESULT 1\n"); return 1; }
    pD3DCompile = (PFN_D3D_COMPILE)GetProcAddress(compiler, "D3DCompile");
    if (!pD3DCompile) { dbg("sweep2: FAIL GetProcAddress(D3DCompile)\n"); dbg("sweep2: RESULT 1\n"); return 1; }

    try_compile("cs-tex-gtid",
        "RWTexture2D<float4> tex : register(u0);\n"
        "[numthreads(4,4,1)] void main(uint3 dtid : SV_GroupThreadID)\n"
        "{ tex[dtid.xy] = float4(1,0,0,1); }\n",
        "cs_5_0");
    try_compile("cs-tex-gidx",
        "RWTexture2D<float4> tex : register(u0);\n"
        "[numthreads(4,4,1)] void main(uint idx : SV_GroupIndex)\n"
        "{ tex[uint2(idx % 4, idx / 4)] = float4(1,0,0,1); }\n",
        "cs_5_0");
    try_compile("cs-tex-gid",
        "RWTexture2D<float4> tex : register(u0);\n"
        "[numthreads(1,1,1)] void main(uint3 gid : SV_GroupID)\n"
        "{ tex[gid.xy] = float4(1,0,0,1); }\n",
        "cs_5_0");
    try_compile("cs-tex-noparam",
        "RWTexture2D<float4> tex : register(u0);\n"
        "[numthreads(1,1,1)] void main() { tex[uint2(2,1)] = float4(0.25, 0.5, 0.75, 1.0); }\n",
        "cs_5_0");
    try_compile("cs-tex-51",
        "RWTexture2D<float4> tex : register(u0);\n"
        "[numthreads(4,4,1)] void main(uint3 dtid : SV_DispatchThreadID)\n"
        "{ tex[dtid.xy] = float4(1,0,0,1); }\n",
        "cs_5_1");
    try_compile("ps-buf-u1",
        "RWStructuredBuffer<uint> buf : register(u1);\n"
        "void main(float4 pos : SV_Position) { buf[0] = 7u; }\n",
        "ps_5_0");
    try_compile("cs-texread",
        "Texture2D<float4> src : register(t0);\n"
        "RWTexture2D<float4> tex : register(u0);\n"
        "[numthreads(1,1,1)] void main() { tex[uint2(1,1)] = src[uint2(0,0)]; }\n",
        "cs_5_0");
    dbg("sweep2: RESULT 0\n");
    return 0;
}
