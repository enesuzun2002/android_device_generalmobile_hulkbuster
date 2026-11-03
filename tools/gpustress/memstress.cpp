/*
 * memstress <total_mb> <threads> <passes>: each thread owns a slice, fills
 * every 4 KiB page (half pseudo-random, half compressible pattern) and
 * verifies all of it on each pass, so pages cycle through swap (zram).
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gPasses;
static long gErrors;
static pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;

struct Slice { uint8_t* mem; size_t pages; int id; };

static uint32_t seedOf(int id, size_t page, int pass) {
    return (uint32_t)(id * 2654435761u) ^ (uint32_t)(page * 40503u) ^ (uint32_t)(pass * 97u) ^ 0x9e3779b9u;
}

static void fill(uint8_t* p, uint32_t s, bool random) {
    uint32_t* w = (uint32_t*)p;
    for (int i = 0; i < 1024; i++) {
        if (random) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; w[i] = s; }
        else w[i] = s + (uint32_t)(i & 15);
    }
}

static void* run(void* arg) {
    Slice* sl = (Slice*)arg;
    uint8_t ref[4096];
    for (int pass = 0; pass < gPasses; pass++) {
        for (size_t pg = 0; pg < sl->pages; pg++)
            fill(sl->mem + pg * 4096, seedOf(sl->id, pg, pass), pg & 1);
        long bad = 0;
        for (size_t pg = 0; pg < sl->pages; pg++) {
            fill(ref, seedOf(sl->id, pg, pass), pg & 1);
            if (memcmp(ref, sl->mem + pg * 4096, 4096)) bad++;
        }
        pthread_mutex_lock(&gLock);
        gErrors += bad;
        printf("thread %d pass %d: %ld bad pages\n", sl->id, pass, bad);
        fflush(stdout);
        pthread_mutex_unlock(&gLock);
    }
    return nullptr;
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: memstress <total_mb> <threads> <passes>\n"); return 2; }
    size_t mb = atol(argv[1]);
    int nt = atoi(argv[2]);
    gPasses = atoi(argv[3]);
    size_t pagesPer = mb * 256 / nt;
    pthread_t th[64];
    Slice sl[64];
    for (int i = 0; i < nt; i++) {
        sl[i].mem = (uint8_t*)malloc(pagesPer * 4096);
        if (!sl[i].mem) { fprintf(stderr, "malloc failed\n"); return 2; }
        sl[i].pages = pagesPer;
        sl[i].id = i;
        pthread_create(&th[i], nullptr, run, &sl[i]);
    }
    for (int i = 0; i < nt; i++) pthread_join(th[i], nullptr);
    printf("total bad pages: %ld %s\n", gErrors, gErrors ? "FAIL" : "PASS");
    return gErrors ? 1 : 0;
}
