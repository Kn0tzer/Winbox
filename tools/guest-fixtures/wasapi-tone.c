/* Raw WASAPI render tone (FD-85 diagnostic): no OpenAL, no mixer. Fills the
 * IAudioClient shared buffer with a known 440 Hz sine directly and reports
 * each step's HRESULT plus the padding behaviour. Mode via BW_TONE_MODE:
 * 0 = polling GetCurrentPadding, 1 = EVENTCALLBACK driven. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const char *hrmsg(HRESULT hr) { static char buf[32]; snprintf(buf, sizeof(buf), "0x%08lx", (unsigned long)hr); return buf; }

int main(void) {
    HRESULT hr;
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioClient *client = NULL;
    IAudioRenderClient *render = NULL;
    WAVEFORMATEX *mix = NULL;
    UINT32 bufferFrames = 0;
    HANDLE event = NULL;
    int mode = 0;
    const char *m = getenv("BW_TONE_MODE");
    if (m) mode = atoi(m);

    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    fprintf(stderr, "WASAPITONE: MODE %d\n", mode);

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) { fprintf(stderr, "WASAPITONE: FAIL CoInitializeEx %s\n", hrmsg(hr)); return 1; }
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&en);
    if (FAILED(hr)) { fprintf(stderr, "WASAPITONE: FAIL enumerator %s\n", hrmsg(hr)); return 1; }
    hr = en->lpVtbl->GetDefaultAudioEndpoint(en, eRender, eConsole, &dev);
    if (FAILED(hr)) { fprintf(stderr, "WASAPITONE: FAIL endpoint %s\n", hrmsg(hr)); return 1; }
    hr = dev->lpVtbl->Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    if (FAILED(hr)) { fprintf(stderr, "WASAPITONE: FAIL activate %s\n", hrmsg(hr)); return 1; }
    hr = client->lpVtbl->GetMixFormat(client, &mix);
    if (FAILED(hr) || !mix) { fprintf(stderr, "WASAPITONE: FAIL mixformat %s\n", hrmsg(hr)); return 1; }
    fprintf(stderr, "WASAPITONE: MIXFMT tag=%u ch=%u rate=%lu bits=%u\n",
        mix->wFormatTag, mix->nChannels, (unsigned long)mix->nSamplesPerSec, mix->wBitsPerSample);

    DWORD flags = 0;
    if (mode == 1) {
        flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        event = CreateEvent(NULL, FALSE, FALSE, NULL);
    }
    hr = client->lpVtbl->Initialize(client, AUDCLNT_SHAREMODE_SHARED, flags, 10000000, 0, mix, NULL);
    if (FAILED(hr)) { fprintf(stderr, "WASAPITONE: FAIL initialize %s\n", hrmsg(hr)); return 1; }
    if (mode == 1) {
        hr = client->lpVtbl->SetEventHandle(client, event);
        if (FAILED(hr)) { fprintf(stderr, "WASAPITONE: FAIL setevent %s\n", hrmsg(hr)); return 1; }
    }
    hr = client->lpVtbl->GetBufferSize(client, &bufferFrames);
    if (FAILED(hr) || !bufferFrames) { fprintf(stderr, "WASAPITONE: FAIL buffersize %s frames=%u\n", hrmsg(hr), bufferFrames); return 1; }
    hr = client->lpVtbl->GetService(client, &IID_IAudioRenderClient, (void **)&render);
    if (FAILED(hr)) { fprintf(stderr, "WASAPITONE: FAIL renderclient %s\n", hrmsg(hr)); return 1; }
    fprintf(stderr, "WASAPITONE: BUFFERFRAMES %u\n", bufferFrames);

    int isFloat = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
        (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         ((WAVEFORMATEXTENSIBLE *)mix)->SubFormat.Data1 == 3);
    int bytesPerSample = mix->wBitsPerSample / 8;
    UINT32 totalFrames = (UINT32)mix->nSamplesPerSec * 3 / 2; /* 1.5 s */
    UINT32 written = 0;
    double phase = 0;
    hr = client->lpVtbl->Start(client);
    if (FAILED(hr)) { fprintf(stderr, "WASAPITONE: FAIL start %s\n", hrmsg(hr)); return 1; }
    fprintf(stderr, "WASAPITONE: PLAYSTART\n");
    int stalls = 0;
    while (written < totalFrames) {
        UINT32 padding = 0;
        hr = client->lpVtbl->GetCurrentPadding(client, &padding);
        if (FAILED(hr)) { fprintf(stderr, "WASAPITONE: FAIL padding %s\n", hrmsg(hr)); return 1; }
        UINT32 avail = bufferFrames > padding ? bufferFrames - padding : 0;
        if (!avail) {
            if (mode == 1) { if (WaitForSingleObject(event, 200) == WAIT_TIMEOUT) stalls++; }
            else Sleep(10);
            continue;
        }
        if (avail > totalFrames - written) avail = totalFrames - written;
        BYTE *data = NULL;
        hr = render->lpVtbl->GetBuffer(render, avail, &data);
        if (FAILED(hr) || !data) { fprintf(stderr, "WASAPITONE: FAIL getbuffer %s\n", hrmsg(hr)); return 1; }
        for (UINT32 i = 0; i < avail; i++) {
            double v = sin(phase) * (isFloat ? 0.366 : 12000.0);
            phase += 2 * 3.14159265358979 * 440.0 / mix->nSamplesPerSec;
            for (WORD ch = 0; ch < mix->nChannels; ch++) {
                BYTE *dst = data + (i * mix->nChannels + ch) * bytesPerSample;
                if (isFloat) { float f = (float)v; memcpy(dst, &f, 4); }
                else if (bytesPerSample == 2) { short s = (short)v; memcpy(dst, &s, 2); }
                else if (bytesPerSample == 4) { int s = (int)(v * 65536.0); memcpy(dst, &s, 4); }
            }
        }
        hr = render->lpVtbl->ReleaseBuffer(render, avail, 0);
        if (FAILED(hr)) { fprintf(stderr, "WASAPITONE: FAIL releasebuffer %s\n", hrmsg(hr)); return 1; }
        written += avail;
    }
    /* Let the tail drain so RESULT means the device consumed the frames. */
    for (int i = 0; i < 100; i++) {
        UINT32 padding = 0;
        if (FAILED(client->lpVtbl->GetCurrentPadding(client, &padding))) break;
        if (!padding) break;
        Sleep(20);
    }
    fprintf(stderr, "WASAPITONE: WROTE %u frames stalls=%d\n", written, stalls);
    client->lpVtbl->Stop(client);
    fprintf(stderr, "WASAPITONE: RESULT 0\n");
    return 0;
}
