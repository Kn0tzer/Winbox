/* ALSA-client tone fixture for the FD-85 continuation.
 *
 * Plays the same 440 Hz square tone the dsptone control plays (same
 * amplitude, 12000/32768) through the guest's ALSA "default" PCM — the
 * exact library leg Wine's winealsa driver uses (mmdevapi -> winealsa ->
 * libasound -> plug -> oss -> /dev/dsp) — without any Wine code above it.
 * If this tone reaches the emulator's audio input, the ALSA leg and its
 * contract with the emulator's OSS device are good and the FD-85 break
 * is inside Wine; if it does not, the break is in this leg.
 *
 * libasound is dlopen()ed so the fixture links against nothing the guest
 * rootfs does not already carry for winealsa itself.
 *
 * Modes (run in sequence, one snd_pcm_open each):
 *   s16       RW_INTERLEAVED writei, S16_LE 48000 stereo
 *   float     RW_INTERLEAVED writei, FLOAT_LE 48000 stereo
 *   s16mmap   MMAP_INTERLEAVED mmap_begin/commit, S16_LE
 *   floatmmap MMAP_INTERLEAVED mmap_begin/commit, FLOAT_LE
 * Every mode prints its negotiated hw params and per-second progress
 * (frames written, avail, delay, state) so a stall is visible in the
 * console exactly where it happens.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef void snd_pcm_t;
typedef void snd_pcm_hw_params_t;
typedef unsigned long uframes_t;
typedef long sframes_t;
typedef struct {
    void *addr;
    unsigned int first;
    unsigned int step;
} channel_area_t;

static int (*p_open)(snd_pcm_t **, const char *, int, int);
static int (*p_close)(snd_pcm_t *);
static int (*p_hw_params_malloc)(snd_pcm_hw_params_t **);
static void (*p_hw_params_free)(snd_pcm_hw_params_t *);
static int (*p_hw_params_any)(snd_pcm_t *, snd_pcm_hw_params_t *);
static int (*p_hw_params_set_access)(snd_pcm_t *, snd_pcm_hw_params_t *, unsigned int);
static int (*p_hw_params_set_format)(snd_pcm_t *, snd_pcm_hw_params_t *, unsigned int);
static int (*p_hw_params_set_channels)(snd_pcm_t *, snd_pcm_hw_params_t *, unsigned int);
static unsigned int (*p_hw_params_set_rate_near)(snd_pcm_t *, snd_pcm_hw_params_t *, unsigned int *, int *);
static int (*p_hw_params_set_period_size_near)(snd_pcm_t *, snd_pcm_hw_params_t *, uframes_t *, int *);
static int (*p_hw_params_set_buffer_size_near)(snd_pcm_t *, snd_pcm_hw_params_t *, uframes_t *);
static int (*p_hw_params_apply)(snd_pcm_t *, snd_pcm_hw_params_t *);
static int (*p_hw_params_get_format)(const snd_pcm_hw_params_t *, unsigned int *);
static int (*p_hw_params_get_access)(const snd_pcm_hw_params_t *, unsigned int *);
static int (*p_hw_params_get_rate)(const snd_pcm_hw_params_t *, unsigned int *, int *);
static int (*p_hw_params_get_channels)(const snd_pcm_hw_params_t *, unsigned int *);
static int (*p_hw_params_get_period_size)(const snd_pcm_hw_params_t *, uframes_t *, int *);
static int (*p_hw_params_get_buffer_size)(const snd_pcm_hw_params_t *, uframes_t *);
static int (*p_prepare)(snd_pcm_t *);
static int (*p_start)(snd_pcm_t *);
static sframes_t (*p_writei)(snd_pcm_t *, const void *, uframes_t);
static sframes_t (*p_avail_update)(snd_pcm_t *);
static int (*p_delay)(snd_pcm_t *, sframes_t *);
static int (*p_state)(snd_pcm_t *);
static int (*p_wait)(snd_pcm_t *, int);
static int (*p_mmap_begin)(snd_pcm_t *, const channel_area_t **, uframes_t *, uframes_t *);
static sframes_t (*p_mmap_commit)(snd_pcm_t *, uframes_t, uframes_t);
static int (*p_drain)(snd_pcm_t *);
static const char *(*p_strerror)(int);
static const char *(*p_format_name)(unsigned int);
static const char *(*p_state_name)(int);

#define LOAD(sym) do { \
    p_##sym = dlsym(alsa, "snd_pcm_" #sym); \
    if (!p_##sym) { fprintf(stderr, "alsatone: FAIL dlsym snd_pcm_%s\n", #sym); return 1; } \
} while (0)

enum {
    STREAM_PLAYBACK = 0,
    ACCESS_MMAP_INTERLEAVED = 0,
    ACCESS_RW_INTERLEAVED = 3,
    FORMAT_S16_LE = 2,
    FORMAT_FLOAT_LE = 14
};

#define RATE 48000
#define CHANNELS 2
#define SECONDS 4
#define AMP_S16 12000
#define AMP_F32 0.3662109375f

static short buf_s16[RATE * SECONDS * CHANNELS];
static float buf_f32[RATE * SECONDS * CHANNELS];

static void fill_buffers(void) {
    unsigned phase = 0;
    for (int i = 0; i < RATE * SECONDS; i++) {
        phase += 440;
        if (phase >= RATE) phase -= RATE;
        int high = phase < RATE / 2;
        buf_s16[i * 2] = high ? AMP_S16 : -AMP_S16;
        buf_s16[i * 2 + 1] = buf_s16[i * 2];
        buf_f32[i * 2] = high ? AMP_F32 : -AMP_F32;
        buf_f32[i * 2 + 1] = buf_f32[i * 2];
    }
}

static int run_mode(const char *name, unsigned int access, unsigned int format, int use_mmap) {
    snd_pcm_t *pcm = NULL;
    snd_pcm_hw_params_t *params = NULL;
    int err = p_open(&pcm, "default", STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "alsatone: MODE %s FAIL open %s\n", name, p_strerror(err));
        return 1;
    }
    p_hw_params_malloc(&params);
    p_hw_params_any(pcm, params);
    if ((err = p_hw_params_set_access(pcm, params, access)) < 0 ||
        (err = p_hw_params_set_format(pcm, params, format)) < 0 ||
        (err = p_hw_params_set_channels(pcm, params, CHANNELS)) < 0) {
        fprintf(stderr, "alsatone: MODE %s FAIL set-params %s\n", name, p_strerror(err));
        return 1;
    }
    unsigned int rate = RATE;
    int dir = 0;
    p_hw_params_set_rate_near(pcm, params, &rate, &dir);
    uframes_t period = 1024;
    p_hw_params_set_period_size_near(pcm, params, &period, &dir);
    uframes_t buffer = 4096;
    p_hw_params_set_buffer_size_near(pcm, params, &buffer);
    if ((err = p_hw_params_apply(pcm, params)) < 0) {
        fprintf(stderr, "alsatone: MODE %s FAIL apply %s\n", name, p_strerror(err));
        return 1;
    }
    unsigned int got_format = 0, got_access = 0, got_rate = 0, got_channels = 0;
    uframes_t got_period = 0, got_buffer = 0;
    p_hw_params_get_format(params, &got_format);
    p_hw_params_get_access(params, &got_access);
    p_hw_params_get_rate(params, &got_rate, NULL);
    p_hw_params_get_channels(params, &got_channels);
    p_hw_params_get_period_size(params, &got_period, NULL);
    p_hw_params_get_buffer_size(params, &got_buffer);
    p_hw_params_free(params);
    fprintf(stderr, "alsatone: MODE %s BEGIN fmt=%s rate=%u ch=%u access=%u period=%lu buffer=%lu\n",
            name, p_format_name(got_format), got_rate, got_channels, got_access,
            (unsigned long)got_period, (unsigned long)got_buffer);
    fflush(stderr);
    if ((err = p_prepare(pcm)) < 0) {
        fprintf(stderr, "alsatone: MODE %s FAIL prepare %s\n", name, p_strerror(err));
        return 1;
    }

    const uframes_t total = (uframes_t)RATE * SECONDS;
    uframes_t written = 0;
    int xruns = 0, waits = 0, wait_timeouts = 0;
    unsigned phase = 0;
    if (use_mmap) {
        /* mmap playback must be explicitly started before the first
         * mmap_begin/commit cycle. */
        if ((err = p_start(pcm)) < 0) {
            fprintf(stderr, "alsatone: MODE %s FAIL start %s\n", name, p_strerror(err));
            return 1;
        }
    }
    if (use_mmap) {
        /* Fixed-cycle probe: the only question is whether mmap_commit
         * ever moves frames through this chain. Always terminates. */
        long total_committed = 0;
        for (int cycle = 0; cycle < 8; cycle++) {
            sframes_t avail = p_avail_update(pcm);
            const channel_area_t *areas = NULL;
            uframes_t offset = 0, frames = got_buffer ? got_buffer : 4096;
            int berr = p_mmap_begin(pcm, &areas, &offset, &frames);
            if (berr < 0) {
                fprintf(stderr, "alsatone: MODE %s CYCLE %d begin FAIL %s avail=%ld\n",
                        name, cycle, p_strerror(berr), (long)avail);
                fflush(stderr);
                break;
            }
            for (uframes_t i = 0; i < frames; i++) {
                phase += 440;
                if (phase >= RATE) phase -= RATE;
                int high = phase < RATE / 2;
                char *sa = (char *)areas[0].addr +
                    (areas[0].first + (offset + i) * areas[0].step) / 8;
                if (format == FORMAT_S16_LE) {
                    short *s = (short *)sa;
                    s[0] = high ? AMP_S16 : -AMP_S16;
                    s[1] = s[0];
                } else {
                    float *s = (float *)sa;
                    s[0] = high ? AMP_F32 : -AMP_F32;
                    s[1] = s[0];
                }
            }
            sframes_t committed = p_mmap_commit(pcm, offset, frames);
            fprintf(stderr, "alsatone: MODE %s CYCLE %d avail=%ld begin offset=%lu frames=%lu committed=%ld state=%s\n",
                    name, cycle, (long)avail, (unsigned long)offset,
                    (unsigned long)frames, (long)committed,
                    p_state_name(p_state(pcm)));
            fflush(stderr);
            if (committed > 0) total_committed += committed;
            p_wait(pcm, 200);
        }
        fprintf(stderr, "alsatone: MODE %s MMAP-SUMMARY total_committed=%ld\n",
                name, total_committed);
        p_close(pcm);
        fprintf(stderr, "alsatone: MODE %s RESULT %d committed=%ld\n",
                name, total_committed > 0 ? 0 : 3, total_committed);
        fflush(stderr);
        return total_committed > 0 ? 0 : 3;
    }
    long idle_loops = 0;
    int begin_zero = 0;
    time_t last_progress = time(NULL);
    while (written < total) {
        uframes_t before = written;
        if (!use_mmap) {
            uframes_t chunk = got_period ? got_period : 1024;
            if (chunk > total - written) chunk = total - written;
            const void *data = (format == FORMAT_S16_LE)
                ? (const void *)(buf_s16 + written * CHANNELS)
                : (const void *)(buf_f32 + written * CHANNELS);
            /* The generated buffers hold the full run (SECONDS seconds). */
            sframes_t done = p_writei(pcm, data, chunk);
            if (done < 0) {
                if (done == -11) { /* -EAGAIN should not happen blocking; pace anyway */
                    if (p_wait(pcm, 100) < 0) wait_timeouts++;
                    continue;
                }
                xruns++;
                fprintf(stderr, "alsatone: MODE %s XRUN %s at written=%lu\n",
                        name, p_strerror((int)done), (unsigned long)written);
                if (p_prepare(pcm) < 0) {
                    fprintf(stderr, "alsatone: MODE %s FAIL reprepare\n", name);
                    return 1;
                }
                continue;
            }
            written += (uframes_t)done;
        } else {
            sframes_t avail = p_avail_update(pcm);
            if (avail < 0) {
                xruns++;
                fprintf(stderr, "alsatone: MODE %s AVAIL-ERR %s at written=%lu\n",
                        name, p_strerror((int)avail), (unsigned long)written);
                if (p_prepare(pcm) < 0 || p_start(pcm) < 0) {
                    fprintf(stderr, "alsatone: MODE %s FAIL recover\n", name);
                    return 1;
                }
                continue;
            }
            if ((uframes_t)avail < (got_period ? got_period : 1024)) {
                waits++;
                if (p_wait(pcm, 200) == 0) wait_timeouts++;
                continue;
            }
            uframes_t want_frames = (uframes_t)avail;
            if (want_frames > total - written) want_frames = total - written;
            const channel_area_t *areas = NULL;
            uframes_t offset = 0, frames = want_frames;
            if ((err = p_mmap_begin(pcm, &areas, &offset, &frames)) < 0) {
                fprintf(stderr, "alsatone: MODE %s FAIL mmap_begin %s\n", name, p_strerror(err));
                return 1;
            }
            if (idle_loops < 3) {
                fprintf(stderr, "alsatone: MODE %s MMAP begin offset=%lu frames=%lu (wanted %lu) area step=%u first=%u\n",
                        name, (unsigned long)offset, (unsigned long)frames,
                        (unsigned long)want_frames, areas ? areas[0].step : 0,
                        areas ? areas[0].first : 0);
                fflush(stderr);
            }
            if (frames == 0) {
                begin_zero++;
                p_wait(pcm, 50);
                if (begin_zero > 100) {
                    sframes_t davail = p_avail_update(pcm);
                    sframes_t ddelay = -1;
                    p_delay(pcm, &ddelay);
                    fprintf(stderr, "alsatone: MODE %s STALL mmap_begin returned 0 frames %d times; avail=%ld delay=%ld state=%s written=%lu\n",
                            name, begin_zero, (long)davail, (long)ddelay,
                            p_state_name(p_state(pcm)), (unsigned long)written);
                    fflush(stderr);
                    return 2;
                }
                continue;
            }
            /* Interleaved areas: one area, step = channels samples. */
            for (uframes_t i = 0; i < frames; i++) {
                phase += 440;
                if (phase >= RATE) phase -= RATE;
                int high = phase < RATE / 2;
                uframes_t idx = offset + i;
                /* Channel-area first/step are in bits. */
                char *sample_addr = (char *)areas[0].addr +
                    (areas[0].first + idx * areas[0].step) / 8;
                if (format == FORMAT_S16_LE) {
                    short *samples = (short *)sample_addr;
                    samples[0] = high ? AMP_S16 : -AMP_S16;
                    samples[1] = samples[0];
                } else {
                    float *samples = (float *)sample_addr;
                    samples[0] = high ? AMP_F32 : -AMP_F32;
                    samples[1] = samples[0];
                }
            }
            sframes_t committed = p_mmap_commit(pcm, offset, frames);
            if (committed < 0) {
                fprintf(stderr, "alsatone: MODE %s FAIL mmap_commit %s\n", name, p_strerror((int)committed));
                return 1;
            }
            if (idle_loops < 3) {
                fprintf(stderr, "alsatone: MODE %s MMAP commit offset=%lu frames=%lu committed=%ld\n",
                        name, (unsigned long)offset, (unsigned long)frames, (long)committed);
                fflush(stderr);
            }
            written += (uframes_t)committed;
        }
        if (written == before) {
            idle_loops++;
            if (time(NULL) - last_progress >= 6) {
                sframes_t davail = p_avail_update(pcm);
                sframes_t ddelay = -1;
                p_delay(pcm, &ddelay);
                fprintf(stderr, "alsatone: MODE %s STALL no progress for 6s; avail=%ld delay=%ld state=%s written=%lu idle=%ld\n",
                        name, (long)davail, (long)ddelay,
                        p_state_name(p_state(pcm)), (unsigned long)written, idle_loops);
                fflush(stderr);
                return 2;
            }
        } else {
            idle_loops = 0;
            last_progress = time(NULL);
        }
        if ((written > 0 && written % RATE == 0) || written == total) {
            sframes_t avail = p_avail_update(pcm);
            sframes_t delay = -1;
            p_delay(pcm, &delay);
            fprintf(stderr, "alsatone: MODE %s T+%lu written=%lu avail=%ld delay=%ld state=%s xruns=%d waits=%d wto=%d\n",
                    name, (unsigned long)(written / RATE), (unsigned long)written,
                    (long)avail, (long)delay, p_state_name(p_state(pcm)), xruns, waits, wait_timeouts);
            fflush(stderr);
        }
    }
    p_drain(pcm);
    p_close(pcm);
    fprintf(stderr, "alsatone: MODE %s RESULT 0 written=%lu xruns=%d\n",
            name, (unsigned long)written, xruns);
    fflush(stderr);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    void *alsa = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!alsa) alsa = dlopen("libasound.so", RTLD_NOW | RTLD_LOCAL);
    if (!alsa) {
        fprintf(stderr, "alsatone: FAIL dlopen libasound: %s\n", dlerror());
        return 1;
    }
    LOAD(open); LOAD(close);
    p_hw_params_malloc = dlsym(alsa, "snd_pcm_hw_params_malloc");
    p_hw_params_free = dlsym(alsa, "snd_pcm_hw_params_free");
    p_hw_params_any = dlsym(alsa, "snd_pcm_hw_params_any");
    p_hw_params_set_access = dlsym(alsa, "snd_pcm_hw_params_set_access");
    p_hw_params_set_format = dlsym(alsa, "snd_pcm_hw_params_set_format");
    p_hw_params_set_channels = dlsym(alsa, "snd_pcm_hw_params_set_channels");
    p_hw_params_set_rate_near = dlsym(alsa, "snd_pcm_hw_params_set_rate_near");
    p_hw_params_set_period_size_near = dlsym(alsa, "snd_pcm_hw_params_set_period_size_near");
    p_hw_params_set_buffer_size_near = dlsym(alsa, "snd_pcm_hw_params_set_buffer_size_near");
    p_hw_params_apply = dlsym(alsa, "snd_pcm_hw_params");
    p_hw_params_get_format = dlsym(alsa, "snd_pcm_hw_params_get_format");
    p_hw_params_get_access = dlsym(alsa, "snd_pcm_hw_params_get_access");
    p_hw_params_get_rate = dlsym(alsa, "snd_pcm_hw_params_get_rate");
    p_hw_params_get_channels = dlsym(alsa, "snd_pcm_hw_params_get_channels");
    p_hw_params_get_period_size = dlsym(alsa, "snd_pcm_hw_params_get_period_size");
    p_hw_params_get_buffer_size = dlsym(alsa, "snd_pcm_hw_params_get_buffer_size");
    LOAD(prepare); LOAD(start); LOAD(writei); LOAD(avail_update); LOAD(delay);
    p_state = dlsym(alsa, "snd_pcm_state");
    LOAD(wait); LOAD(mmap_begin); LOAD(mmap_commit); LOAD(drain);
    p_strerror = dlsym(alsa, "snd_strerror");
    p_format_name = dlsym(alsa, "snd_pcm_format_name");
    p_state_name = dlsym(alsa, "snd_pcm_state_name");
    if (!p_hw_params_malloc || !p_strerror || !p_format_name || !p_state_name || !p_state) {
        fprintf(stderr, "alsatone: FAIL dlsym core symbols\n");
        return 1;
    }

    fprintf(stderr, "alsatone: START v5 (mmap modes first; argv selector)\n");
    fill_buffers();
    int failures = 0;
    const struct { const char *name; unsigned int access, format; int mmap; } modes_all[] = {
        { "s16mmap", ACCESS_MMAP_INTERLEAVED, FORMAT_S16_LE, 1 },
        { "floatmmap", ACCESS_MMAP_INTERLEAVED, FORMAT_FLOAT_LE, 1 },
        { "s16", ACCESS_RW_INTERLEAVED, FORMAT_S16_LE, 0 },
        { "float", ACCESS_RW_INTERLEAVED, FORMAT_FLOAT_LE, 0 },
    };
    /* argv[1]: "mmap" runs only the mmap modes, "writei" only writei. */
    const char *selector = argc > 1 ? argv[1] : "";
    for (int i = 0; i < 4; i++) {
        if (selector[0] == 'm' && !modes_all[i].mmap) continue;
        if (selector[0] == 'w' && modes_all[i].mmap) continue;
        int rc = run_mode(modes_all[i].name, modes_all[i].access, modes_all[i].format, modes_all[i].mmap);
        fprintf(stderr, "alsatone: MODE %s DONE rc=%d\n", modes_all[i].name, rc);
        fflush(stderr);
        if (rc) failures++;
        sleep(1);
    }
    fprintf(stderr, "alsatone: RESULT %d\n", failures ? 1 : 0);
    return failures ? 1 : 0;
}
