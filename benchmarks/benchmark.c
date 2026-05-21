#if !defined(_WIN32)
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "clhash.h"

#if defined(_WIN32)
/* Windows: clock_gettime is not part of the C runtime, so we use
 * QueryPerformanceCounter, which gives a high-resolution monotonic counter. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static inline uint64_t now_ns(void) {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    /* (c * 1e9) / freq, split to avoid overflow on long-running processes. */
    uint64_t sec  = (uint64_t)c.QuadPart / (uint64_t)freq.QuadPart;
    uint64_t frac = (uint64_t)c.QuadPart % (uint64_t)freq.QuadPart;
    return sec * UINT64_C(1000000000)
         + (frac * UINT64_C(1000000000)) / (uint64_t)freq.QuadPart;
}
#else
#include <time.h>
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
#endif

/*
 * Times `test` over several batches of `repeat` calls each, using
 * clock_gettime(CLOCK_MONOTONIC). Reports nanoseconds per byte. A single hash
 * of a few bytes runs in only a handful of nanoseconds — well below the
 * resolution of clock_gettime on most systems — so we always measure a batch
 * and divide. Taking the minimum across batches filters out scheduling jitter
 * the way the old rdtsc-based "best" estimate did.
 */
#define BEST_TIME(test, expected, pre, repeat, size, verbose)                          \
    do {                                                                                \
        const int outer = 5;                                                            \
        if (verbose) printf("%-60s\t: ", #test);                                        \
        fflush(NULL);                                                                   \
        uint64_t best_ns = UINT64_MAX;                                                  \
        uint64_t sum_ns  = 0;                                                           \
        bool mismatch = false;                                                          \
        for (int o = 0; o < outer; o++) {                                               \
            pre;                                                                        \
            uint64_t t0 = now_ns();                                                     \
            for (int i = 0; i < (repeat); i++) {                                        \
                if ((test) != (expected)) { mismatch = true; break; }                   \
            }                                                                           \
            uint64_t t1 = now_ns();                                                     \
            if (mismatch) break;                                                        \
            uint64_t dns = t1 - t0;                                                     \
            if (dns < best_ns) best_ns = dns;                                           \
            sum_ns += dns;                                                              \
        }                                                                               \
        if (mismatch) {                                                                 \
            printf(" not expected ");                                                   \
        } else {                                                                        \
            double S = (double)(size) * (double)(repeat);                               \
            double best_ns_per_byte = (double)best_ns / S;                              \
            double avg_ns_per_byte  = (double)sum_ns / ((double)outer * S);             \
            if (verbose) printf(" %.3f ns per byte (best) \t%.3f ns per byte (avg)\n",  \
                                best_ns_per_byte, avg_ns_per_byte);                     \
            else         printf(" %.3f ", best_ns_per_byte);                            \
        }                                                                               \
        fflush(NULL);                                                                   \
    } while (0)

// looks like java
uint64_t javalikehash(char *input, size_t length) {
  uint64_t sum = 0;
  for(size_t i = 0; i < length; ++i) sum = 31 * sum + (uint64_t) input[i];
  return sum;
}


int main() {
  const int MAXN = 4096;
  const int repeat = 500;
  char * randominput = (char *) malloc(MAXN);
  for(int k = 0; k < MAXN; k++) randominput[k] = rand();
  void * random =  get_random_key_for_clhash(UINT64_C(0x23a23cf5033c3c81),UINT64_C(0xb3816f6a2c68e530));
  printf("# for each input size in bytes, we report the time used to hash a byte\n");
  printf("# First number is the size in bytes\n");
  printf("# Second number is the number of nanoseconds per byte for clhash\n");
  printf("# Third number is the number of nanoseconds per byte for java-like non-random hash function\n");
  for(int size = 8; size < MAXN; ++size) {
        uint64_t hashvalue = clhash(random,randominput, size);
        printf("%20d\t", size);
        BEST_TIME(clhash(random,randominput, size), hashvalue, , repeat, size, false);
        uint64_t javahashvalue = javalikehash(randominput, size);
        printf("\t");
        BEST_TIME(javalikehash(randominput, size), javahashvalue, , repeat, size, false);

        printf("\n");
  }
  free(randominput);
  free(random);
}
