#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef FUZZ_MSAN
#include <stdio.h>
static void fuzz_testmem_msan(void *data, size_t size)
{
    /* This is a trick to force MemorySanitizer to evaluate the data at hand */
    FILE *fp = fopen("/dev/null", "wb");
    fwrite(data, size, 1, fp);
    fclose(fp);
}
#endif

void fuzz_testmem(void *data, size_t size)
{
#ifdef FUZZ_MSAN
    fuzz_testmem_msan(data, size);
#endif
    uint8_t *copy = malloc(size);
    memcpy(copy, data, size);
    free(copy);
}
