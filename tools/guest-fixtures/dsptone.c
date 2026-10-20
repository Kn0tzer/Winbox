/* Direct DSP tone fixture for browser audio transport tests. */
#ifdef BW64_DSPS_TONE_ELF
#include <fcntl.h>
#include <linux/soundcard.h>
#include <math.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void) {
    int dsp = open("/dev/dsp", O_WRONLY);
    if (dsp < 0) {
        perror("dsptone: FAIL open");
        return 1;
    }
    int channels = 2;
    int format = AFMT_S16_LE;
    int rate = 44100;
    if (ioctl(dsp, SNDCTL_DSP_CHANNELS, &channels) < 0 || channels != 2 ||
        ioctl(dsp, SNDCTL_DSP_SETFMT, &format) < 0 || format != AFMT_S16_LE ||
        ioctl(dsp, SNDCTL_DSP_SPEED, &rate) < 0 || rate < 10000 || rate > 48000) {
        fprintf(stderr, "dsptone: FAIL configure channels=%d format=%d rate=%d\n", channels, format, rate);
        close(dsp);
        return 2;
    }
    const size_t frames = (size_t)rate;
    static short samples[48000 * 2];
    for (size_t i = 0; i < frames; i++) {
        short sample = (short)(12000 * sin(2.0 * 3.141592653589793 * 440.0 * i / rate));
        samples[i * 2] = sample;
        samples[i * 2 + 1] = sample;
    }
    fprintf(stderr, "dsptone: WRITESTART rate=%d channels=%d\n", rate, channels);
    fflush(stderr);
    for (int second = 0; second < 6; second++) {
        size_t remaining = frames * 2 * sizeof(short);
        const unsigned char *data = (const unsigned char *)samples;
        while (remaining) {
            ssize_t written = write(dsp, data, remaining);
            if (written <= 0) {
                perror("dsptone: FAIL write");
                close(dsp);
                return 4;
            }
            data += written;
            remaining -= (size_t)written;
        }
    }
    close(dsp);
    fprintf(stderr, "dsptone: RESULT 0\n");
    fflush(stderr);
    return 0;
}
#else
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
    for (unsigned i = 0; i < sizeof(samples); i++) samples[i] = (unsigned char)(128 + 90 * sin(2 * 3.141592653589793 * 440 * i / 11025));
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
#endif
