#include <random>
#include <cstdint>

std::mt19937* mt_rand = NULL;

extern "C" void fuzz_mt19937_init(uint32_t seed) {
    mt_rand = new std::mt19937(seed);
}

extern "C" uint32_t fuzz_mt19937_get(void) {
    return (*mt_rand)();
}

extern "C" void fuzz_mt19937_destroy(void) {
    delete mt_rand;
    mt_rand = NULL;
}
