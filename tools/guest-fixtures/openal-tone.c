/* Shared OpenAL Soft dependency probe.
 * Loads OpenAL32.dll, opens a device, plays a short mono tone and waits for the
 * source to finish. The browser audio probe captures the resulting PCM. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define AL_FORMAT_MONO16 0x1101
#define AL_BUFFER 0x1009
#define AL_FREQUENCY 0x2001
#define AL_SOURCE_STATE 0x1010
#define AL_PLAYING 0x1012
#define AL_STOPPED 0x1014
#define AL_VENDOR 0xB001
#define AL_VERSION 0xB002
#define AL_RENDERER 0xB003

typedef void *(*alcOpenDevice_t)(const char *);
typedef void (*alcCloseDevice_t)(void *);
typedef void *(*alcCreateContext_t)(void *, const int *);
typedef char (*alcMakeContextCurrent_t)(void *);
typedef void (*alcDestroyContext_t)(void *);
typedef const char *(*alcGetString_t)(void *, int);
typedef void (*alGenBuffers_t)(int, unsigned *);
typedef void (*alBufferData_t)(unsigned, int, const void *, int, int);
typedef void (*alGenSources_t)(int, unsigned *);
typedef void (*alSourcei_t)(unsigned, int, int);
typedef void (*alSourcePlay_t)(unsigned);
typedef int (*alGetSourcei_t)(unsigned, int, int *);
typedef int (*alGetError_t)(void);
typedef const char *(*alGetString_t)(int);

int main(void) {
    HMODULE module;
    alcOpenDevice_t alcOpenDevice;
    alcCloseDevice_t alcCloseDevice;
    alcCreateContext_t alcCreateContext;
    alcMakeContextCurrent_t alcMakeContextCurrent;
    alcDestroyContext_t alcDestroyContext;
    alcGetString_t alcGetString;
    alGenBuffers_t alGenBuffers;
    alBufferData_t alBufferData;
    alGenSources_t alGenSources;
    alSourcei_t alSourcei;
    alSourcePlay_t alSourcePlay;
    alGetSourcei_t alGetSourcei;
    alGetError_t alGetError;
    alGetString_t alGetString;
    void *device, *context;
    unsigned buffer = 0, source = 0;
    short *samples;
    int count = 44100 * 3 / 5; /* 0.6 seconds */
    int state = 0;

    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    module = LoadLibraryA("OpenAL32.dll");
    if (!module) { fprintf(stderr, "OPENAL: FAIL load %lu\n", GetLastError()); return 1; }
    #define SYM(name, type) do { \
        *(FARPROC *)&name = GetProcAddress(module, #name); \
        if (!name) { fprintf(stderr, "OPENAL: FAIL symbol " #name "\n"); return 1; } \
    } while (0)
    SYM(alcOpenDevice, alcOpenDevice_t);
    SYM(alcCloseDevice, alcCloseDevice_t);
    SYM(alcCreateContext, alcCreateContext_t);
    SYM(alcMakeContextCurrent, alcMakeContextCurrent_t);
    SYM(alcDestroyContext, alcDestroyContext_t);
    SYM(alcGetString, alcGetString_t);
    SYM(alGenBuffers, alGenBuffers_t);
    SYM(alBufferData, alBufferData_t);
    SYM(alGenSources, alGenSources_t);
    SYM(alSourcei, alSourcei_t);
    SYM(alSourcePlay, alSourcePlay_t);
    SYM(alGetSourcei, alGetSourcei_t);
    SYM(alGetError, alGetError_t);
    SYM(alGetString, alGetString_t);
    #undef SYM

    device = alcOpenDevice(NULL);
    if (!device) { fprintf(stderr, "OPENAL: FAIL open device\n"); return 1; }
    fprintf(stderr, "OPENAL: DEVICE %s\n", alcGetString(device, 0x1005) ? alcGetString(device, 0x1005) : "unknown");
    context = alcCreateContext(device, NULL);
    if (!context || !alcMakeContextCurrent(context)) {
        fprintf(stderr, "OPENAL: FAIL create context\n");
        if (context) alcDestroyContext(context);
        alcCloseDevice(device);
        return 1;
    }
    fprintf(stderr, "OPENAL: VENDOR %s\n", alGetString(AL_VENDOR) ? alGetString(AL_VENDOR) : "unknown");
    fprintf(stderr, "OPENAL: VERSION %s\n", alGetString(AL_VERSION) ? alGetString(AL_VERSION) : "unknown");
    fprintf(stderr, "OPENAL: RENDERER %s\n", alGetString(AL_RENDERER) ? alGetString(AL_RENDERER) : "unknown");

    samples = (short *)malloc(sizeof(short) * count);
    if (!samples) { fprintf(stderr, "OPENAL: FAIL alloc\n"); return 1; }
    for (int i = 0; i < count; i++) samples[i] = (short)(12000 * sin(2 * 3.14159265358979 * 440.0 * i / 44100.0));
    alGenBuffers(1, &buffer);
    alBufferData(buffer, AL_FORMAT_MONO16, samples, count * (int)sizeof(short), 44100);
    alGenSources(1, &source);
    alSourcei(source, AL_BUFFER, (int)buffer);
    fprintf(stderr, "OPENAL: PLAYSTART\n");
    alSourcePlay(source);
    for (int i = 0; i < 60 && state != AL_STOPPED; i++) {
        Sleep(100);
        alGetSourcei(source, AL_SOURCE_STATE, &state);
    }
    if (alGetError() != 0) { fprintf(stderr, "OPENAL: FAIL error %d\n", alGetError()); return 1; }
    if (state != AL_STOPPED) { fprintf(stderr, "OPENAL: FAIL source did not finish\n"); return 1; }
    fprintf(stderr, "OPENAL: RESULT 0\n");
    free(samples);
    alcMakeContextCurrent(NULL);
    alcDestroyContext(context);
    alcCloseDevice(device);
    return 0;
}
