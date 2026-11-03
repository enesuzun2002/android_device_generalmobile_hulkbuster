/* physreg <phys_addr> [value]: read (or write) a 32-bit register via /dev/mem. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: physreg <addr> [value]\n"); return 2; }
    uint32_t addr = strtoul(argv[1], nullptr, 0);
    int fd = open("/dev/mem", argc > 2 ? O_RDWR | O_SYNC : O_RDONLY | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }
    uint32_t page = addr & ~0xfffu;
    void* m = mmap(nullptr, 4096, argc > 2 ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED, fd, page);
    if (m == MAP_FAILED) { perror("mmap"); return 1; }
    volatile uint32_t* r = (volatile uint32_t*)((char*)m + (addr - page));
    if (argc > 2) *r = strtoul(argv[2], nullptr, 0);
    printf("0x%08x = 0x%08x\n", addr, *r);
    return 0;
}
