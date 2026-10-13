/* Default OSS U8/11025Hz mono: verify guest bytes reach browser audio. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    HANDLE dsp = CreateFileA("Z:\\dev\\dsp", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (dsp == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "dsptone: FAIL open %lu\n", GetLastError()); return 1;
    }
    unsigned char samples[11025];
    for (unsigned i = 0; i < sizeof(samples); i++) samples[i] = (unsigned char)(128 + 90 * sin(2 * 3.141592653589793 * 441 * i / 11025));
    fprintf(stderr, "dsptone: WRITESTART\n"); fflush(stderr);
    for (int second = 0; second < 6; second++) {
        DWORD written;
        if (!WriteFile(dsp, samples, sizeof(samples), &written, NULL) || written != sizeof(samples)) {
            fprintf(stderr, "dsptone: FAIL write\n"); CloseHandle(dsp); return 2;
        }
        Sleep(900);
    }
    CloseHandle(dsp);
    fprintf(stderr, "dsptone: RESULT 0\n"); fflush(stderr);
    return 0;
}
