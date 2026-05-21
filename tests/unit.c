#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "clhash.h"

/*
 * This test binary combines four complementary checks:
 * 1) bit-flip sensitivity on small inputs,
 * 2) known-answer vectors for cross-implementation compatibility,
 * 3) collision smoke tests on long messages,
 * 4) avalanche behavior sanity checks.
 */

/* Always-on check, independent of NDEBUG: prints the failing expression and aborts. */
#define assert_true(cond) do {                                                   \
    if (!(cond)) {                                                               \
        fprintf(stderr,                                                          \
                "assertion failed: %s, file %s, line %d\n",                      \
                #cond, __FILE__, __LINE__);                                      \
        abort();                                                                 \
    }                                                                            \
} while (0)




static inline void flipbit ( void * block, int length, uint32_t bit ) {
    uint8_t * b = (uint8_t*)block;
    int byte = bit >> 3;
    assert_true(byte < length);
    bit = bit & 0x7;
    b[byte] ^= (1 << bit);
}

/*
 * Avalanche test:
 * for short messages, flip each input bit and verify hash changes.
 * For very short lengths (<= 8 bytes), also verify the flip-delta pattern is
 * independent of the chosen byte value.
 */
static void clhashavalanchetest() {
    const int N = 1024;
    char * array  = (char*)malloc(N);
    char * array1  = (char*)malloc(N);

    char *  rs = (char*)malloc(RANDOM_BYTES_NEEDED_FOR_CLHASH);
    for(int k = 0; k<N; ++k) {
        array[k] = 0;
        array1[k] = 0;
    }
    for(int k = 0; k<RANDOM_BYTES_NEEDED_FOR_CLHASH; ++k) {
        rs[k] = k+1-k*k;
    }
    int K = 16;
    printf("[clhashavalanchetest] Testing CLHASH avalanche effect.\n");

    for(int bytelength = 1; bytelength < K; ++bytelength ) {

        for(int whichcase = 0; whichcase < 256; ++whichcase) {

            for(int k = 0; k<bytelength; ++k) {
                array[k] = whichcase;
                array1[k] = whichcase + 35;
            }
            uint64_t orighash = clhash(rs, array, bytelength);
            uint64_t orighash1 = clhash(rs, array1, bytelength);

            for(int z = 0; z < 8*bytelength; ++z) {
                flipbit(array,bytelength,z);
                uint64_t newhash = clhash(rs, array, bytelength);
                flipbit(array,bytelength,z);
                assert_true(orighash != newhash);

                flipbit(array1,bytelength,z);
                uint64_t newhash1 = clhash(rs, array1, bytelength);
                flipbit(array1,bytelength,z);
                assert_true(orighash1 != newhash1);

                if((unsigned int) bytelength <= sizeof(uint64_t))
                    assert_true((orighash ^ newhash) == (orighash1 ^ newhash1));

            }
        }
    }
    free(array);
    free(array1);
    free(rs);
    printf("Test passed! \n");
}

// ---------------------------------------------------------------------
// contributed by Eik List
/*
 * Collision-oriented regression test:
 * mutate the last byte of long-ish messages and assert hash output differs.
 */
static void clhashcollisiontest() {
    printf("[clhashcollisiontest] Testing whether we can induce collisions by hacking the right bytes (Eik List's test).\n");
    const size_t NUM_TRIALS = 10;
    const size_t CLNH_NUM_BYTES_PER_BLOCK = 1024;
    const uint8_t KEY_OFFSET = 0x63; // Anything to prevent that K = M

    uint8_t* k = (uint8_t*)malloc(RANDOM_BYTES_NEEDED_FOR_CLHASH);
    size_t j2;

    // Fill key with some deterministic information
    for (j2 = 0; j2 < RANDOM_BYTES_NEEDED_FOR_CLHASH; ++j2) {
        k[j2] = (j2 + KEY_OFFSET) & 0xFF;
    }

    for (size_t i = 1; i < NUM_TRIALS; ++i) {
        for (size_t j = 1; j <= sizeof(uint64_t); ++j) {
            // #Bytes / block + x, with 1 <= x < 8
            const uint64_t mlen = i * CLNH_NUM_BYTES_PER_BLOCK + j;
            uint8_t* m = (uint8_t*)malloc(mlen);

            for (j2 = 0; j2 < mlen; ++j2) {
                m[j2] = j2 & 0xFF;
            }

            const uint64_t actual1 = clhash(k, (const char*)m, mlen);

            // Change final byte
            m[mlen-1] = (m[mlen-1] + 1) & 0xFF;

            const uint64_t actual2 = clhash(k, (const char*)m, mlen);
            const int are_equal = !memcmp(&actual1, &actual2, sizeof(uint64_t));

            if(are_equal) printf("Testing %" PRIu64 " bytes, H1: %" PRIX64 ", H2: %" PRIX64 ", equal: %d \n", mlen, actual1, actual2, are_equal);
            free(m);
            assert_true(!are_equal); // strictly speaking this would not be a bug, but if it happens, it is very likely to be a bug!
        }
    }

    free(k);
    printf("Test passed! \n");
}


/*
 * Basic bit stability test:
 * flipping one bit changes the hash, flipping it back restores the hash.
 */
static void clhashtest() {
    const int N = 1024;
    char * array  = (char*)malloc(N);
    char *  rs = (char*)malloc(RANDOM_BYTES_NEEDED_FOR_CLHASH);
    for(int k = 0; k<N; ++k) {
        array[k] = 0;
    }
    for(int k = 0; k < RANDOM_BYTES_NEEDED_FOR_CLHASH ; ++k) {
        rs[k] = (char) (1-k);
    }

    printf("[clhashtest] checking that flipping a bit changes hash value with clhash \n");
    for(int bit = 0; bit < 64; ++bit ) {
        for(int length = (bit+8)/8; length <= (int)sizeof(uint64_t); ++length) {
            uint64_t x = 0;
            uint64_t orig = clhash(rs, (const char *)&x, length);
            x ^= ((uint64_t)1) << bit;
            uint64_t flip = clhash(rs, (const char *)&x, length);
            assert_true(flip != orig);
            x ^= ((uint64_t)1) << bit;
            uint64_t back = clhash(rs, (const char *)&x, length);
            assert_true(back == orig);
        }
    }

    free(array);
    free(rs);
    printf("Test passed! \n");
}

/*
 * Cross-implementation compatibility test vectors.
 *
 * Each vector is (seed1, seed2, input, length, expected_hash). The random key
 * is built via get_random_key_for_clhash(seed1, seed2), which fills 133
 * 64-bit words with xorshift128+ seeded with (seed1, seed2); see clhash.c.
 * Any port of clhash that reproduces the same key-derivation and core hash
 * must produce the listed expected_hash for the listed input.
 *
 * Inputs come in two flavours:
 *   - A literal byte string given by .data (used when .data != NULL).
 *   - A deterministic pattern of .len bytes (used when .data == NULL):
 *       pattern[i] = (i * 0x9E + 0x37) & 0xFF
 *     This lets us exercise lengths well past 1024 bytes (the long-string
 *     code path) without bloating the test file with literals.
 */
typedef struct {
    uint64_t seed1;
    uint64_t seed2;
    const char *data; /* NULL => use pattern of length len */
    size_t len;
    uint64_t expected;
} clhash_vector_t;

/*
 * Fixed vectors used to validate determinism across ports.
 * If another implementation reproduces key generation and hashing logic,
 * these expected outputs should match exactly.
 */
static const clhash_vector_t known_answer_vectors[] = {
    /* seed (1, 2) -- simple seeds, easy to reproduce from scratch */
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), "",                                            0,  UINT64_C(0x0000000000000000) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), "a",                                           1,  UINT64_C(0xacdafcbac501bd30) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), "ab",                                          2,  UINT64_C(0x59b6ffb15b82bb2f) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), "abc",                                         3,  UINT64_C(0xf56dc687673624d3) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), "1234567",                                     7,  UINT64_C(0xef7c0d4bc4edaeb5) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), "12345678",                                    8,  UINT64_C(0x87a001bd8c8dafff) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), "123456789",                                   9,  UINT64_C(0x5760dd6ecd63f100) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), "abcdefghijklmnop",                            16, UINT64_C(0x7ebb571f25d48fcb) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), "The quick brown fox jumps over the lazy dog", 43, UINT64_C(0x0f6a83d8db4a998c) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), NULL,    64,   UINT64_C(0xacdf8cc8a72bfde4) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), NULL,    100,  UINT64_C(0x1f60236eda78ab3a) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), NULL,    127,  UINT64_C(0x7a5aee3ac782c430) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), NULL,    128,  UINT64_C(0x2862c359af8542ba) },
    /* Lengths around 1024 bytes test the boundary between the short and long code paths. */
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), NULL,    1016, UINT64_C(0x2617c19c67e57316) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), NULL,    1023, UINT64_C(0x3235cc7231b295ae) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), NULL,    1024, UINT64_C(0x1fe72f990185b827) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), NULL,    1025, UINT64_C(0x12ac8f7233faec1a) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), NULL,    1031, UINT64_C(0x05f81daaea50a081) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), NULL,    2048, UINT64_C(0x2d8de569eadfdfd3) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), NULL,    4096, UINT64_C(0x161626acc79338d6) },

    /* seed (0x23a23cf5033c3c81, 0xb3816f6a2c68e530) -- same seeds as demo() */
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), "",                                            0,  UINT64_C(0x0000000000000000) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), "a",                                           1,  UINT64_C(0x4ea7e19b3349b1b4) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), "ab",                                          2,  UINT64_C(0x7986c3e43cb8ed61) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), "abc",                                         3,  UINT64_C(0x48ac6e6f91526149) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), "1234567",                                     7,  UINT64_C(0x3e1e6dc170a084e1) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), "12345678",                                    8,  UINT64_C(0x02ec9d5ed2b10bb4) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), "123456789",                                   9,  UINT64_C(0xb0f75433e605c3bd) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), "abcdefghijklmnop",                            16, UINT64_C(0x9eb5bb194ebe739b) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), "The quick brown fox jumps over the lazy dog", 43, UINT64_C(0xf079e453f4d1d1f7) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), NULL,    64,   UINT64_C(0x769b8d02511a2d8d) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), NULL,    100,  UINT64_C(0x8b6fb22f92dea08c) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), NULL,    127,  UINT64_C(0xa57195473dc2581a) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), NULL,    128,  UINT64_C(0x3a00a31f270bbded) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), NULL,    1016, UINT64_C(0x5a120e562baa8754) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), NULL,    1023, UINT64_C(0xeee727c8f7378128) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), NULL,    1024, UINT64_C(0xe9596a9495802c1e) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), NULL,    1025, UINT64_C(0xa8ed3395c92b425e) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), NULL,    1031, UINT64_C(0xa23ceab428aac1a5) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), NULL,    2048, UINT64_C(0x5121c873b32faede) },
    { UINT64_C(0x23a23cf5033c3c81), UINT64_C(0xb3816f6a2c68e530), NULL,    4096, UINT64_C(0xfe60f0243de4f18e) }
};

static void clhashknownanswertest(void) {
    printf("[clhashknownanswertest] checking prescribed hash values for fixed (key, input) pairs.\n");

    /* Build the deterministic pattern once, sized to the largest vector. */
    size_t maxlen = 0;
    const size_t n = sizeof(known_answer_vectors) / sizeof(known_answer_vectors[0]);
    for (size_t i = 0; i < n; ++i) {
        if (known_answer_vectors[i].data == NULL && known_answer_vectors[i].len > maxlen) {
            maxlen = known_answer_vectors[i].len;
        }
    }
    unsigned char *pattern = NULL;
    if (maxlen > 0) {
        pattern = (unsigned char *)malloc(maxlen);
        assert_true(pattern != NULL);
        for (size_t i = 0; i < maxlen; ++i) {
            pattern[i] = (unsigned char)((i * 0x9E + 0x37) & 0xFF);
        }
    }

    for (size_t i = 0; i < n; ++i) {
        const clhash_vector_t *v = &known_answer_vectors[i];
        void *key = get_random_key_for_clhash(v->seed1, v->seed2);
        assert_true(key != NULL);
        const char *input = (v->data != NULL) ? v->data : (const char *)pattern;
        uint64_t got = clhash(key, input, v->len);
        if (got != v->expected) {
            printf("MISMATCH: seed=(0x%016" PRIx64 ", 0x%016" PRIx64 "), len=%zu, expected 0x%016" PRIx64 ", got 0x%016" PRIx64 "\n",
                   v->seed1, v->seed2, v->len, v->expected, got);
        }
        assert_true(got == v->expected);
        free(key);
    }

    free(pattern);
    printf("Test passed! \n");
}

/*
 * Minimal API usage example mirrored by examples/example.c.
 * Kept here so unit output exercises a tiny end-to-end scenario too.
 */
void demo() {
    // generate random key
    void * random =  get_random_key_for_clhash(UINT64_C(0x23a23cf5033c3c81),UINT64_C(0xb3816f6a2c68e530));
    uint64_t hashvalue1 = clhash(random,"my dog",6);
    uint64_t hashvalue2 = clhash(random,"my cat",6);
    uint64_t hashvalue3 = clhash(random,"my dog",6);
    assert_true(hashvalue1 == hashvalue3);
    assert_true(hashvalue1 != hashvalue2);// very likely to be true

    free(random);
}


int main() {
    /* Keep order stable: cheap tests first, heavier stress tests after. */
    clhashtest();
    clhashknownanswertest();
    clhashcollisiontest();
    clhashavalanchetest();
    demo();
}

