#include <cstdio>
#include <cstdlib>
#include <vector>
#include "clhash.h"

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
    clhasher h(1, 4);
    std::vector<int> vec{1,3,4,5,2,24343};
    int arr[6];
    std::memcpy(arr, vec.data(), sizeof(arr));
    auto hash(h(vec));
    // HPR
    assert_true(h(arr, 6) == hash);
    hash = h("o hai wurld");
    // HPR
    assert_true(hash == h(std::string("o hai wurld")));
    hash = h(vec.data() + 1, vec.size() - 1);
    // HPR
    assert_true(h(vec.data() + 1, vec.size() - 1) == h(std::vector<int>(vec.begin() + 1, vec.end())));
    // HPR
    hash = h(7723291);
    // HPR
    assert_true(h(7723291ULL) != hash); // Changing the size of the object changes the "string".
    printf("code is good.\n");
}
