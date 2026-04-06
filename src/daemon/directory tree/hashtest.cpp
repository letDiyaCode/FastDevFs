// hash_test.cpp
#include "../../../include/daemon/directory tree/hash.h"
#include <iostream>

int main() {
    HashMap hash;           // C++ wrapper around the C implementation
    hash["hello"] = 5;      // uses operator[] -> int&
    hash["hello"]++;        // increments
    std::cout << hash["hello"] << "\n"; // prints 6
    // raw C API also possible:
    // hashmap_t *m = hashmap_create(0);
    // int *p = hashmap_ref(m, "hello");
    // *p = 5;
    // (*p)++;
    // printf("%d\n", *hashmap_ref(m, "hello"));
    return 0;
}
