/* Linux guest/reference contract: fork, private VMAs and shared file remap. */
#include <sys/mman.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    char path[] = "/tmp/bw64-fork-map-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0 || ftruncate(fd, 4096) || unlink(path)) return 1;
    volatile uint32_t* shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    volatile uint32_t* private_map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void* reserved = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((void*)shared == MAP_FAILED || (void*)private_map == MAP_FAILED || reserved == MAP_FAILED) return 2;
    shared[0] = 13; private_map[0] = 17;
    pid_t child = fork();
    if (child < 0) return 3;
    if (!child) {
        void* fresh = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (fresh == MAP_FAILED || fresh == (void*)shared || fresh == (void*)private_map || fresh == reserved) _exit(10);
        if (shared[0] != 13 || private_map[0] != 17) _exit(11);
        shared[0] = 29; private_map[0] = 23;
        volatile uint32_t* replacement = mmap((void*)shared, 4096, PROT_READ | PROT_WRITE,
            MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if ((void*)replacement == MAP_FAILED || replacement[0] != 0) _exit(12);
        replacement[0] = 31;
        _exit(0);
    }
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) {
        printf("fork-mapping: FAIL child status=%d\n", status); return 20;
    }
    if (private_map[0] != 17 || shared[0] != 29) {
        printf("fork-mapping: FAIL private=%u shared=%u\n", private_map[0], shared[0]); return 21;
    }
    puts("fork-mapping: PRIVATE_RESERVATIONS SHARED_ALIAS REMAP_DETACH");
    munmap((void*)shared, 4096); munmap((void*)private_map, 4096); munmap(reserved, 4096); close(fd);
    puts("fork-mapping: RESULT 0");
    return 0;
}
