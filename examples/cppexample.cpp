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
