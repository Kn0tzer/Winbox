/*
 * Machine-definition check: a small purpose-built guest program that
 * reads the emulated machine's answers about itself (total RAM, CPU
 * count, CPU features) and then exercises the two answers this project
 * raised in 2026-11 (4 GiB RAM, 2 vCPUs):
 *
 *   - RAM: mmap 2 GiB -- twice the old 1 GiB machine total -- touch
 *     every page, and verify the written pattern reads back. If the
 *     machine could not actually deliver more than its old advertised
 *     total, the commit or the verification fails.
 *   - CPUs: sched_setaffinity to vCPU 1 only must be accepted (vCPU 1
 *     must be online), and the mask read back must still name exactly
 *     the vCPUs the machine advertises.
 *
 * CPU features are reported, not asserted upward: AVX must read
 * ABSENT, because the emulator does not implement it; advertising it
 * would be a lie a guest would crash on. The check fails if AVX is
 * ever advertised without the implementation landing first.
 *
 * Exit status 0 = every hard check passed. Each failure prints a
 * FAIL line and exits with a distinct nonzero status. Informational
 * faces that the minimal ELF runner does not serve (/proc, /sys)
 * print SKIP instead of failing.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>
#include <cpuid.h>
#include <sys/sysinfo.h>
#include <sys/mman.h>

static int failures = 0;

static void check(int ok, const char* what, int code) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures = code;
    }
}

int main(void) {
    /* ---- RAM: the advertised total ---- */
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        printf("FAIL: sysinfo() itself failed\n");
        return 2;
    }
    unsigned long long total = (unsigned long long)info.totalram * info.mem_unit;
    printf("totalram: %llu bytes\n", total);
    check(total == 4294967296ULL, "sysinfo totalram is 4 GiB", 3);
    check(info.freeram > 0, "sysinfo freeram is nonzero", 4);

    /* ---- RAM: exercise it -- commit 2 GiB and touch every page ---- */
    const size_t span = 2ULL * 1024 * 1024 * 1024;
    unsigned char* p = mmap(NULL, span, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(p != MAP_FAILED, "mmap of 2 GiB (twice the old machine total) succeeds", 5);
    if (p != MAP_FAILED) {
        const size_t page = 4096;
        for (size_t off = 0; off < span; off += page) {
            p[off] = (unsigned char)(off / page);
        }
        unsigned long long sum = 0;
        for (size_t off = 0; off < span; off += page) {
            sum += p[off];
        }
        unsigned long long expect = 0;
        for (size_t i = 0; i < span / page; i++) {
            expect += (unsigned char)i;
        }
        check(sum == expect, "2 GiB committed and verified page by page", 6);
        munmap(p, span);
    }

    /* ---- CPUs: the advertised count, on every face available ---- */
    cpu_set_t set;
    CPU_ZERO(&set);
    int affinityOk = sched_getaffinity(0, sizeof(set), &set) == 0;
    int cpus = affinityOk ? CPU_COUNT(&set) : -1;
    printf("affinity cpus: %d\n", cpus);
    check(cpus == 2, "sched_getaffinity advertises 2 vCPUs", 7);

    long online = sysconf(_SC_NPROCESSORS_ONLN);
    printf("sysconf online cpus: %ld\n", online);
    check(online == 2, "sysconf(_SC_NPROCESSORS_ONLN) is 2", 8);

    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo) {
        char line[512];
        int processors = 0;
        while (fgets(line, sizeof(line), cpuinfo)) {
            if (strncmp(line, "processor\t:", 11) == 0) processors++;
        }
        fclose(cpuinfo);
        printf("cpuinfo processors: %d\n", processors);
        check(processors == 2, "/proc/cpuinfo lists 2 processors", 9);
    } else {
        printf("SKIP: /proc/cpuinfo not served in this runner\n");
    }

    /* ---- CPUs: exercise it -- vCPU 1 must be a real online CPU ---- */
    if (affinityOk) {
        cpu_set_t onlyOne;
        CPU_ZERO(&onlyOne);
        CPU_SET(1, &onlyOne);
        int setOk = sched_setaffinity(0, sizeof(onlyOne), &onlyOne) == 0;
        check(setOk, "sched_setaffinity to vCPU 1 only is accepted", 10);
        if (setOk) {
            cpu_set_t back;
            CPU_ZERO(&back);
            int getOk = sched_getaffinity(0, sizeof(back), &back) == 0;
            check(getOk && CPU_COUNT(&back) == 1 && CPU_ISSET(1, &back),
                  "affinity reads back as exactly vCPU 1", 11);
            sched_setaffinity(0, sizeof(set), &set);
        }
    }

    /* ---- CPU features: the honest advertisement ---- */
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        unsigned int logical = (ebx >> 16) & 0xff;
        printf("cpuid leaf1 logical processors: %u\n", logical);
        check(logical == 2, "CPUID leaf 1 EBX logical-processor count is 2", 12);
        check((ecx & bit_SSE4_2) != 0, "CPUID advertises SSE4.2 (implemented)", 13);
        check((ecx & bit_AVX) == 0,
              "CPUID does NOT advertise AVX (not implemented -- must stay clear)", 14);
    } else {
        check(0, "CPUID leaf 1 unavailable", 15);
    }

    if (failures) {
        printf("machine-definition check: FAILED (%d)\n", failures);
        return failures;
    }
    printf("machine-definition check: ALL PASS\n");
    return 0;
}
