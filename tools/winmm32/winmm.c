/* 32-bit winmm.dll bypass for Boxedwine - routes waveOut directly to Z:\dev\dsp */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

static HANDLE hDsp = INVALID_HANDLE_VALUE;
static WAVEFORMATEX currentFmt = {0};
static DWORD startTime = 0;

static void convert_audio(const short* src, unsigned char* dst, int srcSamples, int* dstSamples) {
    int j = 0;
    int i, k;
    for (i = 0; i < srcSamples; i += 8) {
        int sum = 0;
        int count = 0;
        for (k = 0; k < 8 && (i + k) < srcSamples; k += 2) {
            sum += src[i + k];
            count++;
        }
        if (count > 0) {
            int avg = sum / count;
            int u8 = (avg + 32768) >> 8;
            if (u8 < 0) u8 = 0;
            if (u8 > 255) u8 = 255;
            dst[j++] = (unsigned char)u8;
        }
    }
    *dstSamples = j;
}

__declspec(dllexport) UINT WINAPI waveOutGetNumDevs(void) { return 1; }

__declspec(dllexport) MMRESULT WINAPI waveOutGetDevCapsA(UINT uDeviceID, LPWAVEOUTCAPSA pwoc, UINT cbwoc) {
    if (!pwoc || cbwoc < sizeof(WAVEOUTCAPSA)) return MMSYSERR_INVALPARAM;
    memset(pwoc, 0, sizeof(WAVEOUTCAPSA));
    pwoc->wMid = 1; pwoc->wPid = 1;
    lstrcpyA(pwoc->szPname, "Boxedwine DSP");
    pwoc->dwFormats = WAVE_FORMAT_4S16;
    pwoc->wChannels = 2;
    pwoc->dwSupport = WAVECAPS_SYNC;
    return MMSYSERR_NOERROR;
}

__declspec(dllexport) MMRESULT WINAPI waveOutOpen(LPHWAVEOUT phwo, UINT uDeviceID, LPCWAVEFORMATEX pwfx, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen) {
    if (!phwo || !pwfx) return MMSYSERR_INVALPARAM;
    hDsp = CreateFileA("Z:\\dev\\dsp", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDsp == INVALID_HANDLE_VALUE) return MMSYSERR_ERROR;
    memcpy(&currentFmt, pwfx, sizeof(WAVEFORMATEX));
    *phwo = (HWAVEOUT)0x1234;
    if (!startTime) startTime = GetTickCount();
    return MMSYSERR_NOERROR;
}

__declspec(dllexport) MMRESULT WINAPI waveOutClose(HWAVEOUT hwo) {
    if (hDsp != INVALID_HANDLE_VALUE) { CloseHandle(hDsp); hDsp = INVALID_HANDLE_VALUE; }
    return MMSYSERR_NOERROR;
}

__declspec(dllexport) MMRESULT WINAPI waveOutPrepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    if (!pwh) return MMSYSERR_INVALPARAM;
    pwh->dwFlags |= WHDR_PREPARED; pwh->dwFlags &= ~WHDR_DONE;
    return MMSYSERR_NOERROR;
}

__declspec(dllexport) MMRESULT WINAPI waveOutUnprepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    if (!pwh) return MMSYSERR_INVALPARAM;
    pwh->dwFlags &= ~WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

__declspec(dllexport) MMRESULT WINAPI waveOutWrite(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    if (!pwh || hDsp == INVALID_HANDLE_VALUE) return MMSYSERR_INVALPARAM;
    if (!(pwh->dwFlags & WHDR_PREPARED)) return MMSYSERR_ERROR;
    if (pwh->lpData && pwh->dwBufferLength > 0) {
        int srcSamples = pwh->dwBufferLength / 2;
        int maxDst = srcSamples / 8 + 16;
        unsigned char* dst = (unsigned char*)malloc(maxDst);
        if (dst) {
            int dstSamples = 0;
            DWORD written = 0;
            convert_audio((short*)pwh->lpData, dst, srcSamples, &dstSamples);
            WriteFile(hDsp, dst, dstSamples, &written, NULL);
            free(dst);
        }
    }
    pwh->dwFlags |= WHDR_DONE;
    return MMSYSERR_NOERROR;
}

__declspec(dllexport) MMRESULT WINAPI waveOutPause(HWAVEOUT hwo) { return MMSYSERR_NOERROR; }
__declspec(dllexport) MMRESULT WINAPI waveOutRestart(HWAVEOUT hwo) { return MMSYSERR_NOERROR; }
__declspec(dllexport) MMRESULT WINAPI waveOutReset(HWAVEOUT hwo) { return MMSYSERR_NOERROR; }
__declspec(dllexport) DWORD WINAPI timeGetTime(void) { return GetTickCount() - startTime; }

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) { return TRUE; }
