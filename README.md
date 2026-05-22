# clhash
[![CI](https://github.com/simdhash/clhash/actions/workflows/ci.yml/badge.svg)](https://github.com/simdhash/clhash/actions/workflows/ci.yml)

C library implementing the ridiculously fast CLHash hashing function (with C++ wrappers)


 CLHash is a very fast hashing function that uses
 carry-less multiplication. It runs on recent x64 processors
 (Haswell or better, via PCLMULQDQ + SSE4.2) and on 64-bit ARM
 processors with the crypto extension that provides PMULL
 (e.g., Apple Silicon, modern Cortex-A cores, AWS Graviton).

CLHash has the following characteristics :

* On a recent Intel processor (e.g., Skylake), it can hash input strings at a speed of 0.1 cycles per byte for sufficiently long strings. You read this right: it is simply ridiculously fast.
* On 64-bit ARM with PMULL (e.g., Apple Silicon), it is similarly fast.
* It has strong theoretical guarantees: XOR universality of short strings and excellent almost universality for longer strings.
* The x86 and ARM implementations are bit-for-bit compatible: identical (seed, input) pairs produce the same 64-bit hash on either platform. This is verified by a known-answer test in `tests/unit.c`.

For details, please see the research article:

- Daniel Lemire, Owen Kaser, Faster 64-bit universal hashing using carry-less multiplications, Journal of Cryptographic Engineering 6 (3), 2016. http://arxiv.org/abs/1503.03465


## How fast is it?

A standard fast but non-random hash function is a simple recursive function like so:

```c
uint64_t javalikehash(char *input, size_t length) {
  uint64_t sum = 0;
  for(size_t i = 0; i < length; ++i) sum = 31 * sum + (uint64_t) input[i];
  return sum;
}
```

You should expect clhash to be between **20 to 40 times** faster than this reference
hash function for input spanning hundreds of bytes or more.


## Requirements


* On x64, you need PCLMULQDQ + SSE4.2 (Haswell from 2013 or later in practice). On older
  x64 chips it will either fail to build or be slow. The Makefile and
  CMake build pass `-msse4.2 -mpclmul -march=native` on x86. Virtually all x64 processors 
  today fit this requirement.
* On 64-bit ARM (AArch64), you need the crypto extension that provides the
  PMULL/PMULL2 instructions (advertised via `HWCAP_PMULL` on Linux, and
  always present on Apple Silicon). The build passes `-march=armv8-a+crypto`
  on ARM. Virtually all 64-bit ARM processors support this feature today.

POWER and other architectures are not currently supported; the build will
fail at preprocessing with a clear `#error`.

If your compiler is not C99 compliant... please get better one.


## Usage

 ```bash
 make
 ./unit
 ```
Compile option: if you define CLHASH_BITMIX during compilation, extra work is done to
pass smhasher's avalanche test succesfully. Disabled by default.

## Code sample

```C
#include <assert.h>

#include "clhash.h"

int main() {
    void * random =  get_random_key_for_clhash(UINT64_C(0x23a23cf5033c3c81),UINT64_C(0xb3816f6a2c68e530));
    uint64_t hashvalue1 = clhash(random,"my dog",6);
    uint64_t hashvalue2 = clhash(random,"my cat",6);
    uint64_t hashvalue3 = clhash(random,"my dog",6);
    assert(hashvalue1 == hashvalue3);
    assert(hashvalue1 != hashvalue2);// very likely to be true
    free(random);
    return 0;
}
```

## Simple benchmark

 ```bash
 make
 ./benchmark
 ```

 ## C++

If you prefer the convenience of a C++ interface with support for stl::vector and std::string,
you can create a clhasher object instead.

```C
#include <assert.h>
#include <cstdio>
#include <vector>

#include "clhash.h"


int main(void) {
    clhasher h(UINT64_C(0x23a23cf5033c3c81),UINT64_C(0xb3816f6a2c68e530));
    std::vector<int> vec{1,3,4,5,2,24343};
    uint64_t vechash = h(vec);

    uint64_t arrayhash = h(vec.data(), vec.size());

    assert(vechash == arrayhash);

    uint64_t cstringhash = h("o hai wurld");
    uint64_t stringhash = h(std::string("o hai wurld"));
    assert(cstringhash == stringhash);
}
```

 ```bash
 make
 ./cppunit
 ```



## CMake

You can also build with CMake:

```bash
cmake -S . -B build
cmake --build build -j
```

Run tests with CTest:

```bash
ctest --test-dir build --output-on-failure
```

Enable `CLHASH_BITMIX` at configure time:

```bash
cmake -S . -B build -DCLHASH_ENABLE_BITMIX=ON
cmake --build build -j
```

### Install

The CMake build provides install rules. Pick a prefix and run:

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j
cmake --install build       # may need sudo depending on the prefix
```

This installs:

* `lib/libclhash.a` — the static library
* `include/clhash.h` — the public header (also provides the C++ `clhasher` wrapper)
* `lib/cmake/clhash/clhash{Config,ConfigVersion,Targets}.cmake` — a CMake
  package config so downstream projects can `find_package(clhash)`

Downstream CMake usage looks like:

```cmake
find_package(clhash 1.0 REQUIRED)
add_executable(myapp main.c)
target_link_libraries(myapp PRIVATE clhash::clhash)
```

If you are vendoring clhash via `add_subdirectory()` and do not want the
install rules to be inherited, pass `-DCLHASH_INSTALL=OFF`.

## Drop-in usage (no build system required)

clhash is deliberately tiny: it is a **single C source file** plus a **single
public header**. If you do not want to deal with a build system, just copy

* `src/clhash.c`
* `include/clhash.h`

into your project and compile `clhash.c` with your other sources. The only
requirements are a C99 (or newer) compiler and the right architecture flag:

* `-msse4.2 -mpclmul` (or `-march=native`) on x86-64
* `-march=armv8-a+crypto` on 64-bit ARM

For example, on Apple Silicon:

```bash
cc -O3 -march=armv8-a+crypto -std=c99 -Iinclude -c src/clhash.c
cc -O3 -march=armv8-a+crypto -std=c99 -Iinclude my_program.c clhash.o -o my_program
```

The library has no external dependencies beyond the C standard library.



## Citation

If you use this library in your work, please cite:

```bibtex
@article{lemire2016faster,
    title={Faster 64-bit universal hashing using carry-less multiplications},
    author={Lemire, Daniel and Kaser, Owen},
    journal={Journal of Cryptographic Engineering},
    volume={6},
    number={3},
    pages={171--185},
    year={2016}
}
```
