/* Embedded HTML through mshtml/Wine Gecko.
 * Loads a document from an in-memory stream and reads back parsed DOM text.
 * Without Wine Gecko, mshtml can create the document object but cannot parse
 * HTML; the body text is missing. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <mshtml.h>
#include <stdio.h>
#include <string.h>


static const char html[] =
    "<!doctype html><html><head><title>boxedwine gecko</title></head>"
    "<body><p id=\"mark\">embedded html works</p></body></html>";

static void toUtf8(BSTR value, char *out, size_t size) {
    int length;
    out[0] = 0;
    if (!value) return;
    length = WideCharToMultiByte(CP_UTF8, 0, value, SysStringLen(value), NULL, 0, NULL, NULL);
    if (length <= 0 || (size_t)length >= size) return;
    WideCharToMultiByte(CP_UTF8, 0, value, SysStringLen(value), out, (int)size, NULL, NULL);
    out[length] = 0;
    for (char *p = out; *p; p++) if (*p == '\r' || *p == '\n') *p = ' ';
    while (out[0] == ' ') memmove(out, out + 1, strlen(out));
}

int main(void) {
    IStream *stream = NULL;
    IPersistStreamInit *persist = NULL;
    IHTMLDocument2 *document = NULL;
    IHTMLElement *body = NULL;
    BSTR title = NULL, text = NULL, ready = NULL;
    char titleText[256], bodyText[256];
    HRESULT hr;
    int ok = 0, loaded = 0;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { fprintf(stderr, "MSHTML: FAIL CoInitialize %08lx\n", hr); return 1; }
    hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    if (FAILED(hr)) { fprintf(stderr, "MSHTML: FAIL stream %08lx\n", hr); goto done; }
    hr = IStream_Write(stream, html, sizeof(html) - 1, NULL);
    if (FAILED(hr)) { fprintf(stderr, "MSHTML: FAIL write %08lx\n", hr); goto done; }
    {
        LARGE_INTEGER zero = {0};
        IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    }
    hr = CoCreateInstance(&CLSID_HTMLDocument, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IPersistStreamInit, (void **)&persist);
    if (FAILED(hr)) { fprintf(stderr, "MSHTML: FAIL create %08lx\n", hr); goto done; }
    hr = IPersistStreamInit_InitNew(persist);
    if (FAILED(hr)) { fprintf(stderr, "MSHTML: FAIL initnew %08lx\n", hr); goto done; }
    hr = IPersistStreamInit_Load(persist, stream);
    if (FAILED(hr)) { fprintf(stderr, "MSHTML: FAIL load %08lx\n", hr); goto done; }
    hr = IPersistStreamInit_QueryInterface(persist, &IID_IHTMLDocument2, (void **)&document);
    if (FAILED(hr)) { fprintf(stderr, "MSHTML: FAIL document %08lx\n", hr); goto done; }

    for (int attempt = 0; attempt < 100 && !loaded; attempt++) {
        MSG msg;
        /* mshtml delivers Gecko load callbacks on the apartment thread, so a
         * console app has to pump its message queue for readyState to advance. */
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        hr = IHTMLDocument2_get_readyState(document, &ready);
        if (SUCCEEDED(hr) && ready && (!wcscmp(ready, L"complete") || !wcscmp(ready, L"interactive"))) loaded = 1;
        SysFreeString(ready); ready = NULL;
        if (!loaded) Sleep(100);
    }
    if (!loaded) { fprintf(stderr, "MSHTML: FAIL document never became ready\n"); goto done; }

    hr = IHTMLDocument2_get_title(document, &title);
    toUtf8(SUCCEEDED(hr) ? title : NULL, titleText, sizeof(titleText));
    fprintf(stderr, "MSHTML: TITLE %s\n", titleText);
    hr = IHTMLDocument2_get_body(document, &body);
    if (FAILED(hr) || !body) { fprintf(stderr, "MSHTML: FAIL no body %08lx\n", hr); goto done; }
    hr = IHTMLElement_get_innerText(body, &text);
    toUtf8(SUCCEEDED(hr) ? text : NULL, bodyText, sizeof(bodyText));
    fprintf(stderr, "MSHTML: TEXT %s\n", bodyText);
    if (strcmp(titleText, "boxedwine gecko")) { fprintf(stderr, "MSHTML: FAIL unexpected title\n"); goto done; }
    if (strcmp(bodyText, "embedded html works")) { fprintf(stderr, "MSHTML: FAIL unexpected text\n"); goto done; }
    fprintf(stderr, "MSHTML: RESULT 0\n");
    ok = 1;

done:
    SysFreeString(title); SysFreeString(text); SysFreeString(ready);
    if (body) IHTMLElement_Release(body);
    if (document) IHTMLDocument2_Release(document);
    if (persist) IPersistStreamInit_Release(persist);
    if (stream) IStream_Release(stream);
    CoUninitialize();
    return ok ? 0 : 1;
}
