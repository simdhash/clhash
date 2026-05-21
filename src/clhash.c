#include "clhash.h"

#include <assert.h>
#include <string.h>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <x86intrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
/*
 * ARMv8 NEON port. We provide a thin shim that re-implements the small set of
 * SSE2/SSSE3/PCLMULQDQ intrinsics used by clhash on top of NEON + the PMULL
 * crypto extension (FEAT_PMULL). This keeps the algorithm code below
 * architecture-neutral. Carry-less multiplication uses vmull_p64, which maps
 * to the PMULL/PMULL2 instructions, so the build needs +crypto (or +aes on
 * recent toolchains).
 */
#include <arm_neon.h>

typedef uint64x2_t __m128i;

static inline __m128i _mm_setzero_si128(void) { return vdupq_n_u64(0); }
static inline __m128i _mm_xor_si128(__m128i a, __m128i b) { return veorq_u64(a, b); }
static inline __m128i _mm_or_si128(__m128i a, __m128i b)  { return vorrq_u64(a, b); }
static inline __m128i _mm_and_si128(__m128i a, __m128i b) { return vandq_u64(a, b); }

/* vshlq_n_u64 / vshrq_n_u64 require strict integer literals, but the callers
 * here pass `const int` locals. Use the register-based shift form instead;
 * with a constant n the compiler folds it back to a literal-shift instr. */
#define _mm_slli_epi64(a, n) vshlq_u64((a), vdupq_n_s64((int64_t)(n)))
#define _mm_srli_epi64(a, n) vshlq_u64((a), vdupq_n_s64(-(int64_t)(n)))

/* Byte shifts of the whole 128-bit register. vextq_u8(x, y, n) concatenates
 * x and y and extracts starting at byte n, so we use a zero vector to fill
 * in the bytes shifted in. */
#define _mm_slli_si128(a, imm) \
    vreinterpretq_u64_u8(vextq_u8(vdupq_n_u8(0), vreinterpretq_u8_u64(a), 16 - (imm)))
#define _mm_srli_si128(a, imm) \
    vreinterpretq_u64_u8(vextq_u8(vreinterpretq_u8_u64(a), vdupq_n_u8(0), (imm)))

static inline __m128i _mm_set_epi64x(int64_t hi, int64_t lo) {
    return vcombine_u64(vcreate_u64((uint64_t)lo), vcreate_u64((uint64_t)hi));
}

static inline __m128i _mm_setr_epi32(int32_t a, int32_t b, int32_t c, int32_t d) {
    int32_t tmp[4] = { a, b, c, d };
    return vreinterpretq_u64_s32(vld1q_s32(tmp));
}

static inline __m128i _mm_setr_epi8(char b0, char b1, char b2, char b3,
                                    char b4, char b5, char b6, char b7,
                                    char b8, char b9, char b10, char b11,
                                    char b12, char b13, char b14, char b15) {
    int8_t tmp[16] = { b0, b1, b2, b3, b4, b5, b6, b7,
                       b8, b9, b10, b11, b12, b13, b14, b15 };
    return vreinterpretq_u64_s8(vld1q_s8(tmp));
}

static inline __m128i _mm_cvtsi64_si128(int64_t a) {
    return vsetq_lane_u64((uint64_t)a, vdupq_n_u64(0), 0);
}

static inline int64_t _mm_cvtsi128_si64(__m128i a) {
    return (int64_t)vgetq_lane_u64(a, 0);
}

static inline __m128i _mm_load_si128(const __m128i *p) {
    return vld1q_u64((const uint64_t *)p);
}

static inline __m128i _mm_lddqu_si128(const __m128i *p) {
    return vld1q_u64((const uint64_t *)p);
}

static inline __m128i _mm_loadl_epi64(const __m128i *p) {
    uint64_t lo;
    memcpy(&lo, p, sizeof(lo));
    return vsetq_lane_u64(lo, vdupq_n_u64(0), 0);
}

/* PCLMULQDQ: imm bit 0 picks the half of a, bit 4 picks the half of b. */
#define _mm_clmulepi64_si128(a, b, imm)                                       \
    vreinterpretq_u64_p128(vmull_p64(                                         \
        (poly64_t)vgetq_lane_u64((a), ((imm) & 0x01) ? 1 : 0),                \
        (poly64_t)vgetq_lane_u64((b), ((imm) & 0x10) ? 1 : 0)))

/* SSSE3 byte shuffle.  _mm_shuffle_epi8 differs from vqtbl1q_u8 when an
 * index byte is in 16..127: SSE masks with 0x0F (still indexes into the
 * 16-byte table); NEON returns 0. In clhash this intrinsic is only used by
 * precompReduction64_si128, where the index vector is _mm_srli_si128(Q2,8)
 * and Q2 = clmul(A, 0x1B), so all index bytes are in 0..15 — the two
 * intrinsics coincide there. We still mask to 0x0F to keep the shim
 * semantically equivalent for any caller that respects the high-bit-zero
 * convention. */
static inline __m128i _mm_shuffle_epi8(__m128i table, __m128i indices) {
    uint8x16_t tbl = vreinterpretq_u8_u64(table);
    uint8x16_t idx = vreinterpretq_u8_u64(indices);
    uint8x16_t zero_mask = vcgeq_s8(vreinterpretq_s8_u8(idx), vdupq_n_s8(0)); /* 0xFF where high bit == 0 */
    uint8x16_t masked = vandq_u8(idx, vdupq_n_u8(0x0F));
    uint8x16_t looked_up = vqtbl1q_u8(tbl, masked);
    return vreinterpretq_u64_u8(vandq_u8(looked_up, zero_mask));
}
#else
#error "clhash requires either x86 with PCLMULQDQ or ARMv8 with the crypto/PMULL extension"
#endif

#ifdef __WIN32
#define posix_memalign(p, a, s) (((*(p)) = _aligned_malloc((s), (a))), *(p) ?0 :errno)
#endif



/*
 * High-level overview of clhash():
 * 1) Split input into 64-bit words (+ one optional partial word).
 * 2) Compute a CLMUL-based half-scalar product against random key material.
 * 3) For long inputs, fold 1024-byte chunks with a polynomial multiplier
 *    (lazy reduction in GF(2)).
 * 4) Mix in a length-dependent term so same prefix at different lengths does
 *    not collide systematically.
 * 5) Reduce from 128 bits to final 64-bit hash.
 */


// computes a << 1
static inline __m128i leftshift1(__m128i a) {
    const int x = 1;
    __m128i u64shift =  _mm_slli_epi64(a,x);
    __m128i topbits =  _mm_slli_si128(_mm_srli_epi64(a,64 - x),sizeof(uint64_t));
    return _mm_or_si128(u64shift, topbits);
}

// computes a << 2
static inline __m128i leftshift2(__m128i a) {
    const int x = 2;
    __m128i u64shift =  _mm_slli_epi64(a,x);
    __m128i topbits =  _mm_slli_si128(_mm_srli_epi64(a,64 - x),sizeof(uint64_t));
    return _mm_or_si128(u64shift, topbits);
}

//////////////////
// compute the "lazy" modulo with 2^127 + 2 + 1, actually we compute the
// modulo with (2^128 + 4 + 2) = 2 * (2^127 + 2 + 1) ,
// though  (2^128 + 4 + 2) is not
// irreducible, we have that
//     (x mod (2^128 + 4 + 2)) mod (2^127 + 2 + 1) == x mod (2^127 + 2 + 1)
// That's true because, in general ( x mod k y ) mod y = x mod y.
//
// Precondition:  given that Ahigh|Alow represents a 254-bit value
//                  (two highest bits of Ahigh must be zero)
//////////////////
/*
 * Reduce a 254-bit intermediate represented as (Ahigh || Alow) using a cheap
 * "lazy" modulus step tailored for CLHash's polynomial arithmetic.
 */
static inline __m128i lazymod127(__m128i Alow, __m128i Ahigh) {
    ///////////////////////////////////////////////////
    // CHECKING THE PRECONDITION:
    // Important: we are assuming that the two highest bits of Ahigh
    // are zero. This could be checked by adding a line such as this one:
    // if(_mm_extract_epi64(Ahigh,1) >= (1ULL<<62)){printf("bug\n");abort();}
    //                       (this assumes SSE4.1 support)
    ///////////////////////////////////////////////////
    // The answer is Alow XOR  (  Ahigh <<1 ) XOR (  Ahigh <<2 )
    // This is correct because the two highest bits of Ahigh are
    // assumed to be zero.
    ///////////////////////////////////////////////////
    // credit for simplified implementation : Jan Wassenberg
    __m128i shift1 = leftshift1(Ahigh);
    __m128i shift2 = leftshift2(Ahigh);
    __m128i final =  _mm_xor_si128(_mm_xor_si128(Alow, shift1),shift2);
    return final;
}


// multiplication with lazy reduction
// assumes that the two highest bits of the 256-bit multiplication are zeros
// returns a lazy reduction
/* Carry-less 128x128 multiply followed by lazy reduction back to 128 bits. */
static inline  __m128i mul128by128to128_lazymod127( __m128i A, __m128i B) {
    __m128i Amix1 = _mm_clmulepi64_si128(A,B,0x01);
    __m128i Amix2 = _mm_clmulepi64_si128(A,B,0x10);
    __m128i Alow = _mm_clmulepi64_si128(A,B,0x00);
    __m128i Ahigh = _mm_clmulepi64_si128(A,B,0x11);
    __m128i Amix = _mm_xor_si128(Amix1,Amix2);
    Amix1 = _mm_slli_si128(Amix,8);
    Amix2 = _mm_srli_si128(Amix,8);
    Alow = _mm_xor_si128(Alow,Amix1);
    Ahigh = _mm_xor_si128(Ahigh,Amix2);
    return lazymod127(Alow, Ahigh);
}




// multiply the length and the some key, no modulo
/*
 * Length finalizer term: hashed (keylength, length) pair in GF(2), later XORed
 * into the accumulator so length is part of the hash state.
 */
static __m128i lazyLengthHash(uint64_t keylength, uint64_t length) {
    const __m128i lengthvector = _mm_set_epi64x(keylength,length);
    const __m128i clprod1  = _mm_clmulepi64_si128( lengthvector, lengthvector, 0x10);
    return clprod1;
}


// modulo reduction to 64-bit value. The high 64 bits contain garbage, see precompReduction64
static inline __m128i precompReduction64_si128( __m128i A) {

    //const __m128i C = _mm_set_epi64x(1U,(1U<<4)+(1U<<3)+(1U<<1)+(1U<<0)); // C is the irreducible poly. (64,4,3,1,0)
    const __m128i C = _mm_cvtsi64_si128((1U<<4)+(1U<<3)+(1U<<1)+(1U<<0));
    /*
     * Fast reduction from GF(2^128) to GF(2^64) using a precomputed shuffle
     * strategy. Only the low 64 bits of the result are meaningful.
     */
    __m128i Q2 = _mm_clmulepi64_si128( A, C, 0x01);
    __m128i Q3 = _mm_shuffle_epi8(_mm_setr_epi8(0, 27, 54, 45, 108, 119, 90, 65, (char)216, (char)195, (char)238, (char)245, (char)180, (char)175, (char)130, (char)153),
                                  _mm_srli_si128(Q2,8));
    __m128i Q4 = _mm_xor_si128(Q2,A);
    const __m128i final = _mm_xor_si128(Q3,Q4);
    return final;/// WARNING: HIGH 64 BITS CONTAIN GARBAGE
}


static inline uint64_t precompReduction64( __m128i A) {
    return _mm_cvtsi128_si64(precompReduction64_si128(A));
}


// hashing the bits in value using the keys key1 and key2 (only the first 64 bits of key2 are used).
// This is basically (a xor k1) * (b xor k2) mod p with length component
static uint64_t simple128to64hashwithlength( const __m128i value, const __m128i key, uint64_t keylength, uint64_t length) {
    const __m128i add =  _mm_xor_si128 (value,key);
    const __m128i clprod1  = _mm_clmulepi64_si128( add, add, 0x10);
    const __m128i total = _mm_xor_si128 (clprod1,lazyLengthHash(keylength, length));
    return precompReduction64(total);
}


enum {CLHASH_DEBUG=0};

// For use with CLHASH
// we expect length to have value 128 or, at least, to be divisible by 4.
static __m128i __clmulhalfscalarproductwithoutreduction(const __m128i * randomsource, const uint64_t * string,
        const size_t length) {
    assert(((uintptr_t) randomsource & 15) == 0);// we expect cache line alignment for the keys
    // we expect length = 128, so we need  16 cache lines of keys and 16 cache lines of strings.
    if(CLHASH_DEBUG) assert((length & 3) == 0); // if not, we need special handling (omitted)
    const uint64_t * const endstring = string + length;
    __m128i acc = _mm_setzero_si128();
    // we expect length = 128
    for (; string + 3 < endstring; randomsource += 2, string += 4) {
        const __m128i temp1 = _mm_load_si128( randomsource);
        const __m128i temp2 = _mm_lddqu_si128((__m128i *) string);
        const __m128i add1 = _mm_xor_si128(temp1, temp2);
        const __m128i clprod1 = _mm_clmulepi64_si128(add1, add1, 0x10);
        acc = _mm_xor_si128(clprod1, acc);
        const __m128i temp12 = _mm_load_si128(randomsource + 1);
        const __m128i temp22 = _mm_lddqu_si128((__m128i *) (string + 2));
        const __m128i add12 = _mm_xor_si128(temp12, temp22);
        const __m128i clprod12 = _mm_clmulepi64_si128(add12, add12, 0x10);
        acc = _mm_xor_si128(clprod12, acc);
    }
    if(CLHASH_DEBUG) assert(string == endstring);
    return acc;
}



// the value length does not have to be divisible by 4
/*
 * Same half-scalar product as above, but with explicit handling for 1 or 2
 * trailing 64-bit words.
 */
static __m128i __clmulhalfscalarproductwithtailwithoutreduction(const __m128i * randomsource,
        const uint64_t * string, const size_t length) {
    assert(((uintptr_t) randomsource & 15) == 0);// we expect cache line alignment for the keys
    const uint64_t * const endstring = string + length;
    __m128i acc = _mm_setzero_si128();
    for (; string + 3 < endstring; randomsource += 2, string += 4) {
        const __m128i temp1 = _mm_load_si128(randomsource);
        const __m128i temp2 = _mm_lddqu_si128((__m128i *) string);
        const __m128i add1 = _mm_xor_si128(temp1, temp2);
        const __m128i clprod1 = _mm_clmulepi64_si128(add1, add1, 0x10);
        acc = _mm_xor_si128(clprod1, acc);
        const __m128i temp12 = _mm_load_si128(randomsource+1);
        const __m128i temp22 = _mm_lddqu_si128((__m128i *) (string + 2));
        const __m128i add12 = _mm_xor_si128(temp12, temp22);
        const __m128i clprod12 = _mm_clmulepi64_si128(add12, add12, 0x10);
        acc = _mm_xor_si128(clprod12, acc);
    }
    if (string + 1 < endstring) {
        if(CLHASH_DEBUG) assert(length != 128);
        const __m128i temp1 = _mm_load_si128(randomsource);
        const __m128i temp2 = _mm_lddqu_si128((__m128i *) string);
        const __m128i add1 = _mm_xor_si128(temp1, temp2);
        const __m128i clprod1 = _mm_clmulepi64_si128(add1, add1, 0x10);
        acc = _mm_xor_si128(clprod1, acc);
        randomsource += 1;
        string += 2;
    }
    if (string < endstring) {
        if(CLHASH_DEBUG) assert(length != 128);
        const __m128i temp1 = _mm_load_si128(randomsource);
        const __m128i temp2 = _mm_loadl_epi64((__m128i const*)string);
        const __m128i add1 = _mm_xor_si128(temp1, temp2);
        const __m128i clprod1 = _mm_clmulepi64_si128(add1, add1, 0x10);
        acc = _mm_xor_si128(clprod1, acc);
        if(CLHASH_DEBUG) ++string;
    }
    if(CLHASH_DEBUG) assert(string == endstring);
    return acc;
}
// the value length does not have to be divisible by 4
// additionally folds one extra synthesized word (partial tail packed in LE).
static __m128i __clmulhalfscalarproductwithtailwithoutreductionWithExtraWord(const __m128i * randomsource,
        const uint64_t * string, const size_t length, const uint64_t extraword) {
    assert(((uintptr_t) randomsource & 15) == 0);// we expect cache line alignment for the keys
    const uint64_t * const endstring = string + length;
    __m128i acc = _mm_setzero_si128();
    for (; string + 3 < endstring; randomsource += 2, string += 4) {
        const __m128i temp1 = _mm_load_si128(randomsource);
        const __m128i temp2 = _mm_lddqu_si128((__m128i *) string);
        const __m128i add1 = _mm_xor_si128(temp1, temp2);
        const __m128i clprod1 = _mm_clmulepi64_si128(add1, add1, 0x10);
        acc = _mm_xor_si128(clprod1, acc);
        const __m128i temp12 = _mm_load_si128(randomsource+1);
        const __m128i temp22 = _mm_lddqu_si128((__m128i *) (string + 2));
        const __m128i add12 = _mm_xor_si128(temp12, temp22);
        const __m128i clprod12 = _mm_clmulepi64_si128(add12, add12, 0x10);
        acc = _mm_xor_si128(clprod12, acc);
    }
    if (string + 1 < endstring) {
        const __m128i temp1 = _mm_load_si128(randomsource);
        const __m128i temp2 = _mm_lddqu_si128((__m128i *) string);
        const __m128i add1 = _mm_xor_si128(temp1, temp2);
        const __m128i clprod1 = _mm_clmulepi64_si128(add1, add1, 0x10);
        acc = _mm_xor_si128(clprod1, acc);
        randomsource += 1;
        string += 2;
    }
    // we have to append an extra 1
    if (string < endstring) {
        const __m128i temp1 = _mm_load_si128(randomsource);
        const __m128i temp2 = _mm_set_epi64x(extraword,*string);
        const __m128i add1 = _mm_xor_si128(temp1, temp2);
        const __m128i clprod1 = _mm_clmulepi64_si128(add1, add1, 0x10);
        acc = _mm_xor_si128(clprod1, acc);
    } else {
        const __m128i temp1 = _mm_load_si128(randomsource);
        const __m128i temp2 = _mm_loadl_epi64((__m128i const*)&extraword);
        const __m128i add1 = _mm_xor_si128(temp1, temp2);
        const __m128i clprod1 = _mm_clmulepi64_si128(add1, add1, 0x01);
        acc = _mm_xor_si128(clprod1, acc);
    }
    return acc;
}


static __m128i __clmulhalfscalarproductOnlyExtraWord(const __m128i * randomsource,
        const uint64_t extraword) {
    const __m128i temp1 = _mm_load_si128(randomsource);
    const __m128i temp2 = _mm_loadl_epi64((__m128i const*)&extraword);
    const __m128i add1 = _mm_xor_si128(temp1, temp2);
    const __m128i clprod1 = _mm_clmulepi64_si128(add1, add1, 0x01);
    return clprod1;
}




#ifdef CLHASH_BITMIX
////////
// an invertible function used to mix the bits
// borrowed directly from murmurhash
////////
inline uint64_t fmix64 ( uint64_t k ) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}
#endif



// there always remain an incomplete word that has 1,2, 3, 4, 5, 6, 7 used bytes.
// we append 0s to it
static inline uint64_t createLastWord(const size_t lengthbyte, const uint64_t * lastw) {
    const int significantbytes = lengthbyte % sizeof(uint64_t);
    uint64_t lastword = 0;
    memcpy(&lastword,lastw,significantbytes); // could possibly be faster?
    return lastword;
}




uint64_t clhash(const void* random, const char * stringbyte,
                const size_t lengthbyte) {
    assert(sizeof(size_t)<=sizeof(uint64_t));// otherwise, we need to worry
    assert(((uintptr_t) random & 15) == 0);// we expect cache line alignment for the keys
    const unsigned int  m = 128;// we process the data in chunks of 16 cache lines
    if(CLHASH_DEBUG) assert((m  & 3) == 0); //m should be divisible by 4
    const int m128neededperblock = m / 2;// that is how many 128-bit words of random bits we use per block
    const __m128i * rs64 = (__m128i *) random;
    /*
     * Internal key layout (all coming from get_random_key_for_clhash):
     * - rs64[0 .. m128neededperblock-1]: per-block random words for products.
     * - rs64[m128neededperblock]: polynomial multiplier for long-input folding.
     * - rs64[m128neededperblock+1]: finalization key.
     * - *(uint64_t*)(rs64 + m128neededperblock + 2): length key.
     */
    __m128i polyvalue =  _mm_load_si128(rs64 + m128neededperblock); // to preserve alignment on cache lines for main loop, we pick random bits at the end
    polyvalue = _mm_and_si128(polyvalue,_mm_setr_epi32(0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0x3fffffff));// setting two highest bits to zero
    // we should check that polyvalue is non-zero, though this is best done outside the function and highly unlikely
    const size_t length = lengthbyte / sizeof(uint64_t); // # of complete words
    const size_t lengthinc = (lengthbyte + sizeof(uint64_t) - 1) / sizeof(uint64_t); // # of words, including partial ones

    const uint64_t * string = (const uint64_t *)  stringbyte;
    if (m < lengthinc) { // long strings // modified from length to lengthinc to address issue #3 raised by Eik List
        /* First block initializes accumulator directly. */
        __m128i  acc =  __clmulhalfscalarproductwithoutreduction(rs64, string,m);
        size_t t = m;
        for (; t +  m <= length; t +=  m) {
            // Horner-like fold: acc <- polyvalue * acc XOR block_hash
            acc =  mul128by128to128_lazymod127(polyvalue,acc);
            const __m128i h1 =  __clmulhalfscalarproductwithoutreduction(rs64, string+t,m);
            acc = _mm_xor_si128(acc,h1);
        }
        const int remain = length - t;  // number of completely filled words

        if (remain != 0) {
            // Fold remaining full words plus an optional partial trailing word.
            acc = mul128by128to128_lazymod127(polyvalue, acc);
            if (lengthbyte % sizeof(uint64_t) == 0) {
                const __m128i h1 =
                    __clmulhalfscalarproductwithtailwithoutreduction(rs64,
                            string + t, remain);
                acc = _mm_xor_si128(acc, h1);
            } else {
                const uint64_t lastword = createLastWord(lengthbyte,
                                          (string + length));
                const __m128i h1 =
                    __clmulhalfscalarproductwithtailwithoutreductionWithExtraWord(
                        rs64, string + t, remain, lastword);
                acc = _mm_xor_si128(acc, h1);
            }
        } else if (lengthbyte % sizeof(uint64_t) != 0) {// added to address issue #2 raised by Eik List
            // there are no completely filled words left, but there is one partial word.
            acc = mul128by128to128_lazymod127(polyvalue, acc);
            const uint64_t lastword = createLastWord(lengthbyte, (string + length));
            const __m128i h1 = __clmulhalfscalarproductOnlyExtraWord( rs64, lastword);
            acc = _mm_xor_si128(acc, h1);
        }

        /* Final keyed compression to 64-bit output. */
        const __m128i finalkey = _mm_load_si128(rs64 + m128neededperblock + 1);
        const uint64_t keylength = *(const uint64_t *)(rs64 + m128neededperblock + 2);
        return simple128to64hashwithlength(acc,finalkey,keylength, (uint64_t)lengthbyte);
    } else { // short strings
        /* Single-block path: no polynomial folding needed. */
        if(lengthbyte % sizeof(uint64_t) == 0) {
            __m128i  acc = __clmulhalfscalarproductwithtailwithoutreduction(rs64, string, length);
            const uint64_t keylength = *(const uint64_t *)(rs64 + m128neededperblock + 2);
            acc = _mm_xor_si128(acc,lazyLengthHash(keylength, (uint64_t)lengthbyte));
#ifdef CLHASH_BITMIX
            return fmix64(precompReduction64(acc)) ;
#else
            return precompReduction64(acc) ;
#endif
        }
        const uint64_t lastword = createLastWord(lengthbyte, (string + length));
        __m128i acc = __clmulhalfscalarproductwithtailwithoutreductionWithExtraWord(
                          rs64, string, length, lastword);
        const uint64_t keylength =  *(const uint64_t *)(rs64 + m128neededperblock + 2);
        acc = _mm_xor_si128(acc,lazyLengthHash(keylength, (uint64_t)lengthbyte));
#ifdef CLHASH_BITMIX
        return fmix64(precompReduction64(acc)) ;
#else
        return precompReduction64(acc) ;
#endif
    }
}


/***************************
 * Rest is optional random-number generation stuff
 */


/* Keys for scalar xorshift128. Must be non-zero
These are modified by xorshift128plus.
 */
struct xorshift128plus_key_s {
    uint64_t part1;
    uint64_t part2;
};

typedef struct xorshift128plus_key_s xorshift128plus_key_t;



/**
*
* You can create a new key like so...
*   xorshift128plus_key_t mykey;
*   xorshift128plus_init(324, 4444,&mykey);
*
* or directly if you prefer:
*  xorshift128plus_key_t mykey = {.part1=324,.part2=4444}
*
*  Then you can generate random numbers like so...
*      xorshift128plus(&mykey);
* If your application is threaded, each thread should have its own
* key.
*
*
* The seeds (key1 and key2) should be non-zero. You are responsible for
* checking that they are non-zero.
*
*/
static inline void xorshift128plus_init(uint64_t key1, uint64_t key2, xorshift128plus_key_t *key) {
    key->part1 = key1;
    key->part2 = key2;
}


/*
Return a new 64-bit random number
*/
uint64_t xorshift128plus(xorshift128plus_key_t * key) {
    uint64_t s1 = key->part1;
    const uint64_t s0 = key->part2;
    key->part1 = s0;
    s1 ^= s1 << 23; // a
    key->part2 = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5); // b, c
    return key->part2 + s0;
}

void * get_random_key_for_clhash(uint64_t seed1, uint64_t seed2) {
    xorshift128plus_key_t k;
    xorshift128plus_init(seed1, seed2, &k);
    void * answer;
    if (posix_memalign(&answer, sizeof(__m128i),
                       RANDOM_BYTES_NEEDED_FOR_CLHASH)) {
        return NULL;
    }
    uint64_t * a64 = (uint64_t *) answer;
    for(uint32_t i = 0; i < RANDOM_64BITWORDS_NEEDED_FOR_CLHASH; ++i) {
        a64[i] =  xorshift128plus(&k);
    }
    while((a64[128]==0) && (a64[129]==1)) {
        a64[128] =  xorshift128plus(&k);
        a64[129] =  xorshift128plus(&k);
    }
    return answer;


}



