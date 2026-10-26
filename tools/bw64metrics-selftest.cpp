// tools/bw64metrics-selftest.cpp — native harness for source/util/bw64metrics.cpp.
//
// Build/run from the repo root (or tools/bw64metrics-fixture.sh):
//   g++ -std=c++17 -O2 -Iinclude -DBOXEDWINE_GUEST_X64 \
//       source/util/bw64metrics.cpp source/util/bw64jitstats.cpp \
//       tools/bw64metrics-selftest.cpp -o /tmp/bw64metrics-selftest
//   /tmp/bw64metrics-selftest [fixture-output-path]
//
// With an output path it also writes the emitted line verbatim to that file, so
// web/tests/suite/unit/frame-metrics-contract.test.mjs can check that the real
// writer and the real reader still agree on the schema.
//
// It exercises the real registry (not a copy): JIT publish, stage accounting,
// present windows, JSON shape and the long-stall window reset. klog() is
// captured instead of written to a log.
//
// Disabled mode (BW64_METRICS=0) is checked in fresh child processes, because the
// gate is cached on first use. The children are this same binary started with
// `--child enabled|disabled`; the enabled child is the control.
#include "bw64metrics.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static std::string g_lastLog;

void klog(const char* msg) { g_lastLog = msg ? msg : ""; }

static int failures = 0;
static void expect(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
static void expectContains(const std::string& hay, const char* needle, const char* what) {
    bool ok = hay.find(needle) != std::string::npos;
    std::printf("%s %s (looking for %s)\n", ok ? "PASS" : "FAIL", what, needle);
    if (!ok) failures++;
}

// One window of the same activity a live frame produces, then a flush attempt.
static void driveWindow() {
    Bw64VkFrame f;
    memset(&f, 0, sizeof(f));
    f.frameNo = 1;
    f.wallMs = 16.8;
    bw64_metrics_add_stage(BW64_STAGE_AUDIO, 0.5, 4096);
    bw64_metrics_present_sdl();
    bw64_metrics_vk_frame(&f);
    usleep(600000);
    bw64_metrics_add_stage(BW64_STAGE_AUDIO, 0.5, 4096);
    bw64_metrics_present_sdl();
    bw64_metrics_maybe_emit();
}

// Runs inside a fresh process. BW64_METRICS is read once and cached, so only a
// process that starts with the variable set can observe the disabled state.
static int childMain(const char* mode) {
    const bool wantEnabled = std::strcmp(mode, "enabled") == 0;
    expect(bw64_metrics_enabled() == (wantEnabled ? 1 : 0),
           wantEnabled ? "enabled child: gate reads as enabled" : "disabled child: gate reads as disabled");
    g_lastLog.clear();
    driveWindow();
    if (wantEnabled) {
        expectContains(g_lastLog, "bw64: METRICS", "enabled child: the same activity emits a snapshot");
    } else {
        expect(g_lastLog.empty(), "disabled child: the same activity emits no snapshot");
        // No snapshot is produced: the formatter returns 0 and leaves the buffer alone.
        char json[256];
        std::memset(json, 'x', sizeof(json));
        int n = bw64_metrics_format_json(json, sizeof(json));
        expect(n == 0 && json[0] == 'x', "disabled child: format_json returns 0 and writes nothing");
    }
    std::printf("%s\n", failures ? "CHILD FAILED" : "CHILD PASSED");
    return failures ? 1 : 0;
}

// Spawns this binary as a fresh process with BW64_METRICS set to metricsValue
// (nullptr = not set in the child's environment). Returns true only on exit 0.
static bool runChild(const char* self, const char* mode, const char* metricsValue) {
    std::vector<std::string> env;
    for (char** e = environ; e && *e; ++e) {
        if (std::strncmp(*e, "BW64_METRICS=", 13) != 0) env.push_back(*e);
    }
    if (metricsValue) env.push_back(std::string("BW64_METRICS=") + metricsValue);
    std::vector<char*> envp;
    for (std::string& s : env) envp.push_back(&s[0]);
    envp.push_back(nullptr);
    // /proc/self/exe: the harness is Linux-only, and a fresh exec is the only way
    // to get a clean metrics gate (fork() would copy the parent's cached value).
    char* childArgv[] = {const_cast<char*>(self), const_cast<char*>("--child"), const_cast<char*>(mode), nullptr};
    std::fflush(stdout);
    pid_t pid = 0;
    if (posix_spawn(&pid, "/proc/self/exe", nullptr, nullptr, childArgv, envp.data()) != 0) return false;
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(int argc, char** argv) {
    if (argc > 2 && std::strcmp(argv[1], "--child") == 0) return childMain(argv[2]);

    // Disabled mode is a no-op (no log line, no snapshot). The gate is cached
    // per process, so both halves run in fresh children, before this process
    // starts its own window. The enabled child is the control: if it did not
    // emit, the disabled "no output" check would pass for the wrong reason.
    expect(runChild(argv[0], "enabled", "1"), "control: a fresh BW64_METRICS=1 process emits for the same activity");
    expect(runChild(argv[0], "disabled", "0"), "fresh BW64_METRICS=0 process emits nothing and records nothing");

    const char* fixtureOut = argc > 1 ? argv[1] : nullptr;
    setenv("BW64_METRICS", "1", 1);
    char json[6144];

    // Start a window.
    bw64_metrics_format_json(json, sizeof(json));
    usleep(60000);

    Bw64JitStats jit;
    memset(&jit, 0, sizeof(jit));
    jit.structSize = sizeof(jit);
    jit.engine = "emitted-wasm";
    jit.available = 1;
    jit.enabled = 1;
    jit.blocksEmitted = 912;
    jit.blocksInstantiated = 904;
    jit.blocksEntered = 33210;
    jit.instructionsRetired = 81234567ULL;
    jit.fallbacks = 12;
    jit.emitMicroseconds = 8120;
    jit.instantiateMicroseconds = 2450;
    bw64_jit_stats_publish(BW64_METRICS_ARCH64, &jit);

    bw64_metrics_add_stage(BW64_STAGE_SYSCALL, 18.4, 0);
    bw64_metrics_add_stage(BW64_STAGE_AUDIO, 0.5, 4096);
    bw64_metrics_add_stage(BW64_STAGE_AUDIO, 0.5, 4096);

    Bw64VkFrame f;
    memset(&f, 0, sizeof(f));
    f.frameNo = 7;
    f.wallMs = 16.8;
    f.submitMs = 0.9;
    f.trapOtherMs = 1.2;
    f.presentMs = 0.3;
    f.hopMs = 0.4;
    f.bufMs = 0.2;
    f.imgMs = 0.5;
    f.guestMs = 14.4;
    f.trapCalls = 31;
    f.hopChunks = 12;
    f.hopBytes = 81234;
    bw64_metrics_vk_frame(&f);

    int n = bw64_metrics_format_json(json, sizeof(json));
    expect(n > 100 && n < (int)sizeof(json), "snapshot fits and is non-empty");
    std::string snap(json);
    expectContains(snap, "\"arch\":\"x86-64\"", "arch comes from the build");
    expectContains(snap, "\"path\":\"vk\"", "Vulkan present path selected");
    expectContains(snap, "\"count\":1", "one present counted");
    expectContains(snap, "\"fps\":", "fps is emitted");
    expectContains(snap, "\"submitMs\":0.90", "submit ms is the measured value");
    expectContains(snap, "\"guestMs\":14.40", "guest span is the measured value");
    expectContains(snap, "\"calls\":2,\"ms\":1.00,\"bytes\":8192", "audio calls/ms/bytes aggregate");
    expectContains(snap, "\"calls\":1,\"ms\":18.40", "syscall span aggregates");
    expectContains(snap,
                  "\"cpu64\":{\"supported\":true,\"available\":true,\"engine\":\"emitted-wasm\",\"enabled\":true",
                  "cpu64 JIT section reflects the published stats");
    expectContains(snap, "\"instructionsRetired\":81234567", "retired-instruction counter is forwarded");
    expectContains(snap, "\"cpu32\":{\"supported\":false,\"available\":false,\"engine\":null", "absent engine is explicit");
    expectContains(snap, "\"id\":\"wine\"", "guest layers are reported unavailable");

    // Window is consumed: a second snapshot with no activity must not repeat it.
    usleep(60000);
    bw64_metrics_format_json(json, sizeof(json));
    snap = json;
    expect(snap.find("\"count\":0") != std::string::npos, "window counters reset after a snapshot");

    // Long-stall reset: activity after >2 s must not divide by the stall.
    usleep(2100000);
    g_lastLog.clear();
    bw64_metrics_add_stage(BW64_STAGE_AUDIO, 1.0, 1024);
    bw64_metrics_maybe_emit();
    expect(g_lastLog.empty(), "first event after a long stall starts a new window (no stale emit)");
    usleep(550000);
    bw64_metrics_add_stage(BW64_STAGE_AUDIO, 1.0, 1024);
    bw64_metrics_maybe_emit();
    expectContains(g_lastLog, "bw64: METRICS", "bridge-only activity emits a snapshot");
    // The stalled audio call must be gone; only the fresh call remains.
    expect(g_lastLog.find("\"audio\":{\"supported\":true,\"calls\":1,") != std::string::npos,
           "stale window was dropped, fresh window has calls=1");


    // Emit one representative snapshot for the reader-side contract test. The
    // window is built from the same calls a live frame makes, so the fixture is
    // the writer's real output, not a hand-written shape.
    if (fixtureOut) {
        g_lastLog.clear();
        // Build one window's worth of activity, then let the registry EMIT it:
        // the fixture is a real emitted line with a real window, not a snapshot
        // taken by hand (a hand-taken snapshot has a ~0 ms window, so the
        // registry correctly refuses to report fps/frameMs for it).
        bw64_metrics_add_stage(BW64_STAGE_SYSCALL, 18.4, 0);
        bw64_metrics_add_stage(BW64_STAGE_AUDIO, 0.5, 4096);
        bw64_metrics_add_stage(BW64_STAGE_AUDIO, 0.5, 4096);
        bw64_metrics_add_stage(BW64_STAGE_GL, 2.25, 0);
        bw64_metrics_vk_frame(&f);
        usleep(600000);                       // a genuine >=500 ms window
        bw64_metrics_add_stage(BW64_STAGE_SYSCALL, 0.25, 0);
        if (g_lastLog.empty()) {
            std::printf("FAIL the registry emitted no fixture line\n");
            failures++;
        } else {
            // Normalise the ONE clock field that carries no measurement meaning,
            // so a regenerated fixture stays byte-stable: uptimeMs is milliseconds
            // since this host process started.
            std::string stable(g_lastLog);
            const size_t at = stable.find("\"uptimeMs\":");
            if (at != std::string::npos) {
                size_t end = stable.find(',', at);
                if (end != std::string::npos) stable.replace(at, end - at, "\"uptimeMs\":12345");
            }
            FILE* out = std::fopen(fixtureOut, "wb");
            if (!out) {
                std::printf("FAIL cannot open fixture output %s\n", fixtureOut);
                failures++;
            } else {
                std::fputs(stable.c_str(), out);
                std::fputc('\n', out);
                std::fclose(out);
                std::printf("PASS wrote fixture to %s\n", fixtureOut);
            }
        }
    }

    std::printf("%s\n", failures ? "SELFTEST FAILED" : "SELFTEST PASSED");
    return failures ? 1 : 0;
}
