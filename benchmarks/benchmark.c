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
#include <math.h>
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

uint64_t javalikehash(char *input, size_t length);

/*
 * Measure "best" nanoseconds per byte over several batches.
 * Returns a negative value if a validation mismatch is detected.
 */
static double measure_best_ns_per_byte_clhash(const void *key,
                                              const char *input,
                                              size_t length,
                                              int repeat) {
    const int outer = 5;
    const uint64_t expected = clhash(key, input, length);
    uint64_t best_ns = UINT64_MAX;

    for (int o = 0; o < outer; ++o) {
        uint64_t t0 = now_ns();
        for (int i = 0; i < repeat; ++i) {
            if (clhash(key, input, length) != expected) {
                return -1.0;
            }
        }
        uint64_t t1 = now_ns();
        uint64_t dns = t1 - t0;
        if (dns < best_ns) best_ns = dns;
    }

    return (double)best_ns / ((double)length * (double)repeat);
}

static double measure_best_ns_per_byte_javalike(const char *input,
                                                size_t length,
                                                int repeat) {
    const int outer = 5;
    const uint64_t expected = javalikehash((char *)input, length);
    uint64_t best_ns = UINT64_MAX;

    for (int o = 0; o < outer; ++o) {
        uint64_t t0 = now_ns();
        for (int i = 0; i < repeat; ++i) {
            if (javalikehash((char *)input, length) != expected) {
                return -1.0;
            }
        }
        uint64_t t1 = now_ns();
        uint64_t dns = t1 - t0;
        if (dns < best_ns) best_ns = dns;
    }

    return (double)best_ns / ((double)length * (double)repeat);
}

// looks like java
uint64_t javalikehash(char *input, size_t length) {
  uint64_t sum = 0;
  for(size_t i = 0; i < length; ++i) sum = 31 * sum + (uint64_t) input[i];
  return sum;
}


int main() {
  const int MAXN = 4096*16;
  const int repeat = 500;
  char * randominput = (char *) malloc(MAXN);
  for(int k = 0; k < MAXN; k++) randominput[k] = rand();
  void * random =  get_random_key_for_clhash(UINT64_C(0x23a23cf5033c3c81),UINT64_C(0xb3816f6a2c68e530));
  printf("# for each input size in bytes, we report the time used to hash a byte\n");
  printf("# First number is the size in bytes\n");
  printf("# Second number is the number of nanoseconds per byte for clhash\n");
  printf("# Third number is the number of nanoseconds per byte for java-like non-random hash function\n");
  printf("# Fourth number is baseline/clhash ratio\n");
  for(int size = 8; size < MAXN; size*=3) {
        double clhash_ns_per_byte = measure_best_ns_per_byte_clhash(random, randominput, (size_t)size, repeat);
        double baseline_ns_per_byte = measure_best_ns_per_byte_javalike(randominput, (size_t)size, repeat);
        double ratio = baseline_ns_per_byte / clhash_ns_per_byte;

        printf("%20d\t", size);
        if ((clhash_ns_per_byte < 0.0) || (baseline_ns_per_byte < 0.0) || !isfinite(ratio)) {
            printf("not expected\tnot expected\tnot expected\n");
        } else {
            printf("%.3f\t%.3f\t%.3f\n", clhash_ns_per_byte, baseline_ns_per_byte, ratio);
        }
  }
  free(randominput);
  free(random);
}
