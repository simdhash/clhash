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
