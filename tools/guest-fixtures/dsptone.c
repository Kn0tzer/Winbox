/* Direct DSP tone fixture for browser audio transport tests. */
#ifdef BW64_DSPS_TONE_ELF
#include <linux/soundcard.h>
/* Use direct Linux syscalls so 32-bit glibc startup does not mask DSP results. */
static long syscall3(long number, long a, long b, long c) {
    long result;
#if defined(__x86_64__)
    __asm__ volatile("syscall" : "=a"(result) : "0"(number), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
#else
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(number), "b"(a), "c"(b), "d"(c)
                     : "memory", "cc");
#endif
    return result;
}

static long syscall1(long number, long a) {
    return syscall3(number, a, 0, 0);
}

static void write_all(int fd, const char* data, unsigned long length) {
    while (length) {
#if defined(__x86_64__)
        long written = syscall3(1, fd, (long)data, (long)length);
#else
        long written = syscall3(4, fd, (long)data, (long)length);
#endif
        if (written <= 0) return;
        data += written;
        length -= (unsigned long)written;
    }
}

static void exit_guest(int code) {
#if defined(__x86_64__)
    syscall1(60, code);
#else
    syscall1(1, code);
#endif
    for (;;) { }
}

static short samples[48000 * 2];

int guest_main(void) {
    static const char path[] = "/dev/dsp";
#if defined(__x86_64__)
    long dsp = syscall3(2, (long)path, 1, 0);
#else
    long dsp = syscall3(5, (long)path, 1, 0);
#endif
    if (dsp < 0) {
        static const char message[] = "dsptone: FAIL open\n";
        write_all(2, message, sizeof(message) - 1);
        exit_guest(1);
    }
    int channels = 2;
    int format = AFMT_S16_LE;
    int rate = 44100;
#if defined(__x86_64__)
    long channelResult = syscall3(16, dsp, SNDCTL_DSP_CHANNELS, (long)&channels);
    long formatResult = syscall3(16, dsp, SNDCTL_DSP_SETFMT, (long)&format);
    long rateResult = syscall3(16, dsp, SNDCTL_DSP_SPEED, (long)&rate);
#else
    long channelResult = syscall3(54, dsp, SNDCTL_DSP_CHANNELS, (long)&channels);
    long formatResult = syscall3(54, dsp, SNDCTL_DSP_SETFMT, (long)&format);
    long rateResult = syscall3(54, dsp, SNDCTL_DSP_SPEED, (long)&rate);
#endif
    if (channelResult < 0 || channels != 2 || formatResult < 0 || format != AFMT_S16_LE ||
        rateResult < 0 || rate < 10000 || rate > 48000) {
        static const char message[] = "dsptone: FAIL configure\n";
        write_all(2, message, sizeof(message) - 1);
#if defined(__x86_64__)
        syscall1(3, dsp);
#else
        syscall1(6, dsp);
#endif
        exit_guest(2);
    }
    const unsigned long frames = (unsigned long)rate;
    unsigned phase = 0;
    for (unsigned long i = 0; i < frames; i++) {
        phase += 440;
        if (phase >= (unsigned)rate) phase -= (unsigned)rate;
        short sample = phase < (unsigned)rate / 2 ? 12000 : -12000;
        samples[i * 2] = sample;
        samples[i * 2 + 1] = sample;
    }
    static const char startMessage[] = "dsptone: WRITESTART rate=44100 channels=2\n";
    write_all(2, startMessage, sizeof(startMessage) - 1);
    for (int second = 0; second < 6; second++) {
        unsigned long remaining = frames * 2 * sizeof(short);
        const unsigned char *data = (const unsigned char *)samples;
        while (remaining) {
#if defined(__x86_64__)
            long written = syscall3(1, dsp, (long)data, (long)remaining);
#else
            long written = syscall3(4, dsp, (long)data, (long)remaining);
#endif
            if (written <= 0) {
                static const char message[] = "dsptone: FAIL write\n";
                write_all(2, message, sizeof(message) - 1);
#if defined(__x86_64__)
                syscall1(3, dsp);
#else
                syscall1(6, dsp);
#endif
                exit_guest(4);
            }
            data += written;
            remaining -= (unsigned long)written;
        }
    }
#if defined(__x86_64__)
    syscall1(3, dsp);
#else
    syscall1(6, dsp);
#endif
    static const char resultMessage[] = "dsptone: RESULT 0\n";
    write_all(2, resultMessage, sizeof(resultMessage) - 1);
    return 0;
}

#if defined(__x86_64__)
__asm__(".global _start\n_start:\n andq $-16, %rsp\n call guest_main\n"
        " movl %eax, %edi\n movl $60, %eax\n syscall\n");
#else
__asm__(".global _start\n_start:\n andl $-16, %esp\n call guest_main\n"
        " movl %eax, %ebx\n movl $1, %eax\n int $0x80\n");
#endif
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
