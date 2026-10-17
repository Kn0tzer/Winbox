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
    BOOL arial = FALSE;
    int font_metrics = 0;
    if (collection && SUCCEEDED(collection->FindFamilyName(L"Arial", &index, &arial)) && arial) {
        IDWriteFontFamily* family = NULL;
        IDWriteFont* regular = NULL;
        IDWriteFontFace* face = NULL;
        if (SUCCEEDED(collection->GetFontFamily(index, &family)) &&
            SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &regular))) {
            DWRITE_FONT_METRICS fm = {};
            regular->GetMetrics(&fm);
            UINT32 character = 'A';
            UINT16 glyph = 0;
            if (SUCCEEDED(regular->CreateFontFace(&face)) &&
                SUCCEEDED(face->GetGlyphIndices(&character, 1, &glyph)))
                font_metrics = fm.designUnitsPerEm > 0 && fm.ascent > 0 && glyph != 0;
        }
        if (face) face->Release();
        if (regular) regular->Release();
        if (family) family->Release();
    }
    if (!arial || !font_metrics) result = 4;
    char message[256];
    sprintf(message, "win32-fonts: GDI=%d FAMILIES=%u TAHOMA=%d MAPPING=%08lx ARIAL=%d METRICS=%d RESULT %d\n",
        gdi, families, exists, static_cast<unsigned long>(mapping), arial, font_metrics, result);
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
