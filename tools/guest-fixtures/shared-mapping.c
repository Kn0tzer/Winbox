/* Linux guest: MAP_SHARED writer placement and unlink/recreate identity. */
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    const char* path = "/tmp/tmpmap-bw64-regression";
    unlink(path);
    int old_fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (old_fd < 0 || ftruncate(old_fd, 4096)) return 1;
    volatile uint32_t* writer = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, old_fd, 0);
    volatile uint32_t* reader = mmap((void*)0x7ffe0000, 4096, PROT_READ, MAP_SHARED | MAP_FIXED, old_fd, 0);
    if ((void*)writer == MAP_FAILED || (void*)reader == MAP_FAILED) return 2;
    writer[0] = 0x12345678;
    if (reader[0] != writer[0]) { puts("shared-mapping: FAIL writer alias"); return 10; }
    puts("shared-mapping: WRITER_ALIAS");
    if (unlink(path)) return 3;
    int new_fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (new_fd < 0 || ftruncate(new_fd, 4096)) return 4;
    struct stat old_stat, new_stat;
    if (fstat(old_fd, &old_stat) || fstat(new_fd, &new_stat) || old_stat.st_ino == new_stat.st_ino) return 5;
    volatile uint32_t* fresh = mmap((void*)0x7ffd0000, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, new_fd, 0);
    if ((void*)fresh == MAP_FAILED) return 6;
    if (fresh[0] != 0 || reader[0] != 0x12345678) { puts("shared-mapping: FAIL file identity"); return 20; }
    fresh[0] = 0xabcdef01;
    if (reader[0] != 0x12345678) { puts("shared-mapping: FAIL recreated file alias"); return 21; }
    volatile uint32_t* private_map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, old_fd, 0);
    if ((void*)private_map == MAP_FAILED) return 7;
    private_map[0] = 0x11111111;
    if (reader[0] != 0x12345678) { puts("shared-mapping: FAIL private map alias"); return 22; }
    puts("shared-mapping: FILE_IDENTITY");
    munmap((void*)writer, 4096); munmap((void*)reader, 4096); munmap((void*)fresh, 4096); munmap((void*)private_map, 4096);
    close(old_fd); close(new_fd); unlink(path);
    puts("shared-mapping: RESULT 0");
    return 0;
}
