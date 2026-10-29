/* fmt12: guest-visible format-support facts behind vkd3d's
 * d3d12_device_determine_additional_typed_uav_support gate.
 * Prints OPTIONS.TypedUAVLoadAdditionalFormats, OPTIONS1.Int64ShaderOps,
 * and per-format Support2.UAV_TYPED_LOAD for the 18 required formats. */
#define COBJMACROS
#define INITGUID
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "d3d12.h"
#include "dxgi1_6.h"

static const struct { DXGI_FORMAT f; const char *name; } kReq[] = {
    { DXGI_FORMAT_R32_FLOAT, "R32_FLOAT" }, { DXGI_FORMAT_R32_UINT, "R32_UINT" },
    { DXGI_FORMAT_R32_SINT, "R32_SINT" },
    { DXGI_FORMAT_R32G32B32A32_FLOAT, "R32G32B32A32_FLOAT" },
    { DXGI_FORMAT_R32G32B32A32_UINT, "R32G32B32A32_UINT" },
    { DXGI_FORMAT_R32G32B32A32_SINT, "R32G32B32A32_SINT" },
    { DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT" },
    { DXGI_FORMAT_R16G16B16A16_UINT, "R16G16B16A16_UINT" },
    { DXGI_FORMAT_R16G16B16A16_SINT, "R16G16B16A16_SINT" },
    { DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM" },
    { DXGI_FORMAT_R8G8B8A8_UINT, "R8G8B8A8_UINT" },
    { DXGI_FORMAT_R8G8B8A8_SINT, "R8G8B8A8_SINT" },
    { DXGI_FORMAT_R16_FLOAT, "R16_FLOAT" }, { DXGI_FORMAT_R16_UINT, "R16_UINT" },
    { DXGI_FORMAT_R16_SINT, "R16_SINT" },
    { DXGI_FORMAT_R8_UNORM, "R8_UNORM" }, { DXGI_FORMAT_R8_UINT, "R8_UINT" },
    { DXGI_FORMAT_R8_SINT, "R8_SINT" },
};

int main(void)
{
    IDXGIFactory4 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    ID3D12Device *dev = NULL;
    HRESULT hr;
    int all = 1;
    unsigned i;

    hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
    if (FAILED(hr)) { fprintf(stderr, "fmt12: FAIL factory hr=0x%08lx\n", hr); return 1; }
    IDXGIFactory4_EnumAdapters1(factory, 0, &adapter);
    hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
    if (FAILED(hr)) { fprintf(stderr, "fmt12: FAIL device hr=0x%08lx\n", hr); return 1; }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS opt;
        memset(&opt, 0, sizeof(opt));
        hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS, &opt, sizeof(opt));
        fprintf(stderr, "fmt12: OPTIONS hr=0x%08lx TypedUAVLoadAdditionalFormats=%d\n",
                hr, (int)opt.TypedUAVLoadAdditionalFormats);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 opt1;
        memset(&opt1, 0, sizeof(opt1));
        hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS1, &opt1, sizeof(opt1));
        fprintf(stderr, "fmt12: OPTIONS1 hr=0x%08lx Int64ShaderOps=%d WaveOps=%d\n",
                hr, (int)opt1.Int64ShaderOps, (int)opt1.WaveOps);
    }
    for (i = 0; i < sizeof(kReq) / sizeof(kReq[0]); i++) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT fs;
        int tl;
        memset(&fs, 0, sizeof(fs));
        fs.Format = kReq[i].f;
        hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs));
        tl = SUCCEEDED(hr) && (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD);
        if (!tl) all = 0;
        fprintf(stderr, "fmt12: FMT %-24s hr=0x%08lx S1=0x%08x S2=0x%08x UAV_TYPED_LOAD=%d\n",
                kReq[i].name, hr, fs.Support1, fs.Support2, tl);
    }
    fprintf(stderr, "fmt12: ALL18_TYPED_LOAD=%d\n", all);
    fprintf(stderr, "fmt12: RESULT 0\n");
    return 0;
}
