/*
 * CLHash: 64-bit universal hashing.
 *
 * CLHash is designed for very high throughput on modern CPUs with carry-less
 * multiplication support.
 *
 * Reference:
 * Daniel Lemire, Owen Kaser, "Faster 64-bit universal hashing using
 * carry-less multiplications", Journal of Cryptographic Engineering, 2016.
 *
 * Hardware note:
 * This implementation targets modern x86-64 processors and includes ARM paths.
 * Performance and portability characteristics depend on architecture support.
 *
 * Compile option:
 * If CLHASH_BITMIX is defined at compile time, additional mixing is enabled to
 * improve avalanche behavior (e.g., for smhasher-style tests). Disabled by default.
 */

#ifndef INCLUDE_CLHASH_H_
#define INCLUDE_CLHASH_H_


#include <stdlib.h>
#include <stdint.h> // life is short, please use a C99-compliant compiler
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CLHash requires a fixed-size random key (133 64-bit words = 1064 bytes).
 * Reuse the same key across many inputs for deterministic hashes.
 */
enum {RANDOM_64BITWORDS_NEEDED_FOR_CLHASH=133,RANDOM_BYTES_NEEDED_FOR_CLHASH=133*8};



/**
 * Compute a 64-bit hash.
 *
 * Parameters:
 * - random:
 *   Pointer to the CLHash key material. It must point to at least
 *   RANDOM_BYTES_NEEDED_FOR_CLHASH bytes.
 *   For best performance, 16-byte alignment is recommended.
 *   Typical usage is to generate this once, then reuse it for many calls.
 *
 * - stringbyte:
 *   Input byte buffer to hash. It may point to arbitrary binary data.
 *
 * - lengthbyte:
 *   Input length in bytes.
 *
 * Returns:
 * - 64-bit hash value.
 *
 * Minimal usage example (also see examples/example.c):
 *
 *   void *key = get_random_key_for_clhash(seed1, seed2);
 *   uint64_t h1 = clhash(key, "my dog", 6);
 *   uint64_t h2 = clhash(key, "my cat", 6);
 *   free(key);
 */
uint64_t clhash(const void* random, const char * stringbyte,
                const size_t lengthbyte);



/**
 * Generate deterministic CLHash key material from two 64-bit seeds.
 *
 * Returns:
 * - Pointer to an allocated key buffer suitable for clhash().
 *
 * Ownership:
 * - Caller owns the returned pointer and must release it with free().
 *
 * Notes:
 * - Same (seed1, seed2) => same key => same hash outputs for same inputs.
 * - Different seeds are expected to produce different keys/hashes.
 */
void * get_random_key_for_clhash(uint64_t seed1, uint64_t seed2);

#ifdef __cplusplus
} // extern "C"
#endif

#ifdef __cplusplus
#include <vector>
#include <string>
#include <cstring> // For std::strlen

struct clhasher {
    /*
     * RAII helper for C++ users:
     * - allocates key data in constructor
     * - frees key data in destructor
     */
    const void *random_data_;
    clhasher(uint64_t seed1=137, uint64_t seed2=777): random_data_(get_random_key_for_clhash(seed1, seed2)) {}

    /* Hash an array of T. len is an element count, not a byte count. */
    template<typename T>
    uint64_t operator()(const T *data, const size_t len) const {
        return clhash(random_data_, (const char *)data, len * sizeof(T));
    }

    /* Hash a null-terminated C string (without the final '\0'). */
    uint64_t operator()(const char *str) const {return operator()(str, std::strlen(str));}

    /* Hash an object by raw bytes of its in-memory representation. */
    template<typename T>
    uint64_t operator()(const T &input) const {
        return operator()((const char *)&input, sizeof(T));
    }

    /* Hash std::vector contents as contiguous bytes. */
    template<typename T>
    uint64_t operator()(const std::vector<T> &input) const {
        return operator()((const char *)input.data(), sizeof(T) * input.size());
    }

    /* Hash std::string contents (without implicit null terminator). */
    uint64_t operator()(const std::string &str) const {
        return operator()(str.data(), str.size());
    }
    ~clhasher() {
        std::free((void *)random_data_);
    }
};
#endif // #ifdef __cplusplus

#endif /* INCLUDE_CLHASH_H_ */
