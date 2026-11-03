/* WinMM waveOut tone probe (FD-85 diagnostic): plays a 440 Hz mono sine
 * through waveOut and reports whether WHDR_DONE ever fires. Bypasses OpenAL
 * so the Wine audio-driver path can be measured on its own. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    HWAVEOUT out = NULL;
    WAVEFORMATEX fmt;
    WAVEHDR hdr;
    short *samples;
    int count = 44100 * 3 / 5; /* 0.6 s */
    MMRESULT rc;
    int i;

    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    fprintf(stderr, "WINMMTONE: NUMDEVS %u\n", waveOutGetNumDevs());

    samples = (short *)malloc(sizeof(short) * count);
    if (!samples) { fprintf(stderr, "WINMMTONE: FAIL alloc\n"); return 1; }
    for (i = 0; i < count; i++) samples[i] = (short)(12000 * sin(2 * 3.14159265358979 * 440.0 * i / 44100.0));

    ZeroMemory(&fmt, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = 44100;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    rc = waveOutOpen(&out, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
    if (rc != MMSYSERR_NOERROR || !out) { fprintf(stderr, "WINMMTONE: FAIL open %u\n", rc); return 1; }
    ZeroMemory(&hdr, sizeof(hdr));
    hdr.lpData = (LPSTR)samples;
    hdr.dwBufferLength = count * (int)sizeof(short);
    rc = waveOutPrepareHeader(out, &hdr, sizeof(hdr));
    if (rc != MMSYSERR_NOERROR) { fprintf(stderr, "WINMMTONE: FAIL prepare %u\n", rc); return 1; }
    fprintf(stderr, "WINMMTONE: PLAYSTART\n");
    rc = waveOutWrite(out, &hdr, sizeof(hdr));
    if (rc != MMSYSERR_NOERROR) { fprintf(stderr, "WINMMTONE: FAIL write %u\n", rc); return 1; }
    for (i = 0; i < 100 && !(hdr.dwFlags & WHDR_DONE); i++) Sleep(100);
    if (!(hdr.dwFlags & WHDR_DONE)) {
        fprintf(stderr, "WINMMTONE: FAIL done never fired flags=%lx\n", hdr.dwFlags);
        return 1;
    }
    fprintf(stderr, "WINMMTONE: RESULT 0\n");
    waveOutUnprepareHeader(out, &hdr, sizeof(hdr));
    waveOutClose(out);
    free(samples);
    return 0;
}
