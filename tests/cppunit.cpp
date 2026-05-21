#include <cstdio>
#include <cstdlib>
#include <vector>
#include "clhash.h"

/*
 * C++ wrapper smoke test:
 * verifies that the clhasher overloads are consistent across equivalent inputs.
 */

/* Always-on check, independent of NDEBUG: prints the failing expression and aborts. */
#define assert_true(cond) do {                                                   \
    if (!(cond)) {                                                               \
        std::fprintf(stderr,                                                     \
                "assertion failed: %s, file %s, line %d\n",                      \
                #cond, __FILE__, __LINE__);                                      \
        std::abort();                                                            \
    }                                                                            \
} while (0)


int main(void) {
    /* Fixed seeds => deterministic test values across runs. */
    clhasher h(1, 4);

    /* Same content via std::vector and raw array must hash identically. */
    std::vector<int> vec{1,3,4,5,2,24343};
    int arr[6];
    std::memcpy(arr, vec.data(), sizeof(arr));
    auto hash(h(vec));
    assert_true(h(arr, 6) == hash);

    /* C string and std::string overloads should agree on byte content. */
    hash = h("o hai wurld");
    assert_true(hash == h(std::string("o hai wurld")));

    /* Pointer+length view and sliced std::vector should produce same hash. */
    hash = h(vec.data() + 1, vec.size() - 1);
    assert_true(h(vec.data() + 1, vec.size() - 1) == h(std::vector<int>(vec.begin() + 1, vec.end())));

    /* Object overload hashes raw object bytes, so int and unsigned long long differ. */
    hash = h(7723291);
    assert_true(h(7723291ULL) != hash); // Changing the size of the object changes the "string".
    printf("code is good.\n");
}
