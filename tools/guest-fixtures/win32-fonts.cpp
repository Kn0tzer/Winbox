// Chromium's default-font setup needs a real DirectWrite Tahoma family.
#include <windows.h>
#include <dwrite.h>
#include <stdio.h>

int main() {
    LOGFONTW logical = {};
    logical.lfHeight = -14;
    logical.lfWeight = FW_NORMAL;
    lstrcpyW(logical.lfFaceName, L"Tahoma");
    HDC dc = GetDC(NULL);
    HFONT font = CreateFontIndirectW(&logical);
    HGDIOBJ previous = dc && font ? SelectObject(dc, font) : NULL;
    TEXTMETRICW metrics = {};
    int gdi = dc && previous && GetTextMetricsW(dc, &metrics) && metrics.tmHeight > 0;
    int result = gdi ? 0 : 1;

    IDWriteFactory* factory = NULL;
    IDWriteFontCollection* collection = NULL;
    IDWriteGdiInterop* interop = NULL;
    IDWriteFont* mapped = NULL;
    BOOL exists = FALSE;
    UINT32 index = 0, families = 0;
    HRESULT mapping = E_FAIL;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory));
    if (SUCCEEDED(hr)) hr = factory->GetSystemFontCollection(&collection, FALSE);
    if (SUCCEEDED(hr)) {
        families = collection->GetFontFamilyCount();
        hr = collection->FindFamilyName(L"Tahoma", &index, &exists);
    }
    if (FAILED(hr) || !exists || !families) result = 2;
    if (factory && SUCCEEDED(factory->GetGdiInterop(&interop)))
        mapping = interop->CreateFontFromLOGFONT(&logical, &mapped);
    if (FAILED(mapping) || !mapped) result = 3;
    char message[192];
    sprintf(message, "win32-fonts: GDI=%d FAMILIES=%u TAHOMA=%d MAPPING=%08lx RESULT %d\n",
        gdi, families, exists, static_cast<unsigned long>(mapping), result);
    fputs(message, stdout);
    fflush(stdout);
    OutputDebugStringA(message);
    if (mapped) mapped->Release();
    if (interop) interop->Release();
    if (collection) collection->Release();
    if (factory) factory->Release();
    if (previous) SelectObject(dc, previous);
    if (font) DeleteObject(font);
    if (dc) ReleaseDC(NULL, dc);
    return result;
}
