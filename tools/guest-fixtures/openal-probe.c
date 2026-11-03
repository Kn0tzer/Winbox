/* OpenAL boundary probe (FD-85 payload lane): the stock openal-tone fixture
 * plus prints of the sample buffer's pointer and content immediately before
 * and after alBufferData, from the fixture's own code. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <AL/al.h>
#include <AL/alc.h>
#include <windows.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RATE 44100
#define FREQ 440.0
#define DURATION 0.6
#define AMPLITUDE 12000

static double bufabs(const short *s, int n) {
    double a = 0;
    int i;
    for (i = 0; i < n; i++) { double v = s[i]; a += v < 0 ? -v : v; }
    return a;
}

int main(void)
{
    ALCdevice *dev;
    ALCcontext *ctx;
    ALuint buffer, source;
    ALenum err;
    ALint state;
    int count = (int)(RATE * DURATION);
    short *samples;
    int i, waited;

    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    samples = (short *)malloc((size_t)count * sizeof(short));
    if (!samples) { printf("OPENAL: FAIL malloc\n"); return 1; }
    for (i = 0; i < count; i++)
        samples[i] = (short)(sin(2.0 * M_PI * FREQ * i / RATE) * AMPLITUDE);
    printf("OAP: ptr=%p filled head=%d %d %d %d absum=%.1f\n",
           (void *)samples, samples[100], samples[101], samples[102], samples[103],
           bufabs(samples, count));

    dev = alcOpenDevice(NULL);
    if (!dev) { printf("OPENAL: FAIL alcOpenDevice\n"); return 1; }
    {
        const ALCchar *name = alcGetString(dev, ALC_DEVICE_SPECIFIER);
        printf("OPENAL: DEVICE %s\n", name ? (const char *)name : "?");
    }
    ctx = alcCreateContext(dev, NULL);
    if (!ctx || !alcMakeContextCurrent(ctx)) {
        printf("OPENAL: FAIL context\n");
        return 1;
    }
    printf("OPENAL: VENDOR %s\n", alGetString(AL_VENDOR));
    printf("OPENAL: VERSION %s\n", alGetString(AL_VERSION));
    printf("OPENAL: RENDERER %s\n", alGetString(AL_RENDERER));

    alGenBuffers(1, &buffer);
    alBufferData(buffer, AL_FORMAT_MONO16, samples, count * (int)sizeof(short), RATE);
    printf("OAP: post-upload head=%d %d %d %d absum=%.1f\n",
           samples[100], samples[101], samples[102], samples[103],
           bufabs(samples, count));
    err = alGetError();
    if (err != AL_NO_ERROR) { printf("OPENAL: FAIL alBufferData err=%x\n", err); return 1; }

    alGenSources(1, &source);
    alSourcei(source, AL_BUFFER, (ALint)buffer);
    alSourcef(source, AL_GAIN, 1.0f);
    alSourcePlay(source);
    printf("OPENAL: PLAYSTART\n");

    waited = 0;
    for (;;) {
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        if (state == AL_STOPPED) break;
        if (waited > 20000) { printf("OPENAL: FAIL source did not finish\n"); return 1; }
        Sleep(10);
        waited += 10;
    }
    printf("OPENAL: RESULT 0\n");
    return 0;
}
