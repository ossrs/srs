#include <stdint.h>
void fuzz_mt19937_init(uint32_t seed);
uint32_t fuzz_mt19937_get(void);
void fuzz_mt19937_destroy(void);
