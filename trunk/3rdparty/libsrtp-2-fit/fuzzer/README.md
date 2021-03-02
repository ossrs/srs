# libsrt fuzzer

By Guido Vranken <guidovranken@gmail.com> -- https://guidovranken.wordpress.com/

This is an advanced fuzzer for libSRTP (https://github.com/cisco/libsrtp). It implements several special techniques, described below, that are not often found in fuzzers or elsewhere. All are encouraged to transpose these ideas to their own fuzzers for the betterment of software security.

Feel free to contact me for business enquiries.

## Building

From the repository's root directory:

```sh
CC=clang CXX=clang++ CXXFLAGS="-fsanitize=fuzzer-no-link,address,undefined -g -O3" CFLAGS="-fsanitize=fuzzer-no-link,address,undefined -g -O3" LDFLAGS="-fsanitize=fuzzer-no-link,address,undefined" ./configure
LIBFUZZER="-fsanitize=fuzzer" make srtp-fuzzer
```

## Features

### Portable PRNG

```mt19937.c``` exports the C++11 Mersenne Twister implementaton. Hence, a modern C++ compiler is required to compile this file.

This approach has the following advantages:

- rand() is fickle -- its behavior eg. the sequence of numbers that it generates for a given seed, may differ across systems and libc's.
- C++11 mt19937 is portable, meaning that its behavior will be consistent across platforms. This is important to keep the fuzzing corpus portable.
- No need to implement a portable PRNG ourselves, or risk license incompatability by importing it from other projects.

### Size 0 allocations

To test whether allocations of size 0 eg. ```malloc(0)``` are ever dereferenced and written to, the custom allocater will return an intentionally invalid pointer pointer address for these requests.

For more information, see the comments in ```fuzz_alloc()```.

### Random allocation failures

The custom allocator will periodically return ```NULL``` for heap requests. This tests the library's resilience and correct operation in the event of global memory shortages.

The interval of ```NULL``` return values is deterministic as it relies on the PRNG, so for a given fuzzer input (that encodes the PRNG seed as well), behavior of that input with regards to allocator behaviour is consistent, allowing for reliable reproduction of bugs.

### Detecting inadequate pointer arithmetic

This feature is only available on 32 bit builds.

Unless the ```--no_mmap``` flag is given, the fuzzer will use a special allocation technique for some of the allocation requests. It will use ```mmap()``` to reserve memory at the extremities of the virtual address space -- sometimes at 0x00010000 and sometimes at 0xFFFF0000. This approach can assist in detecting invalid or inadequate pointer arithmetic. For example, consider the following code:

```c
if ( start + n < end ) {
    memset(start, 0, n);
}
```

where ```start``` and ```end``` demarcate a memory region, and ```n``` is some positive integer.
If ```n``` is a sufficiently large value, a pointer addition overflow will occur, leading to a page fault. By routinely placing allocations at the high virtual address ```0xFFFF0000```, the chances of detecting this bug are increased. So let's say ```start``` was previously allocated at ```0xFFFF0000```, and ```end``` is ```0xFFFF1000```, and ```n``` is 0xFFFFF. Then the expression effectively becomes:

```c
if ( 0xFFFF0000 + 0x000FFFFF < 0xFFFF1000 ) {
    memset(0xFFFF0000, 0, 0x000FFFF);
}
```

The addition ```0xFFFF0000 + 0x000FFFFF``` overflows so the result is ```0x000EFFFF```. Hence:

```c
if ( 0x000EFFFF < 0xFFFF1000 ) { // Expression resolves as true !
```

The subsequent ```memset``` is executed contrary to the programmer's intentions, and a segmentation fault will occur.

While this is a corner case, it can not be ruled out that it might occur in a production environment. What's more, the analyst examining the crash can reason about how the value of ```n``` comes about in the first place, and concoct a crafted input that leads to a very high ```n``` value, making the "exploit" succeed even with average virtual addresses.

Aside from using ```mmap``` to allocate at address ```0xFFFF0000```, the fuzzer will also place allocations at the low virtual address ```0x00010000``` to detect invalid pointer arithmetic involving subtraction:

```c
if ( end - n > start ) {
```

### Output memory testing

```testmem.c``` exports ```fuzz_testmem```. All this function does is copy the input buffer to a newly allocated heap region, and then free that heap region. If AddressSanitizer is enabled, this ensures that the input buffer to ```fuzz_testmem``` is a legal memory region.
If MemorySanitizer is enabled, then ``fuzz_testmem``` calls ```fuzz_testmem_msan````. The latter function writes the data at hand to ```/dev/null```. This is an nice trick to make MemorySanitizer evaluate this data, and crash if it contains uninitialized bytes.
This function has been implemented in a separate file for a reason: from the perspective of an optimizing compiler, this is a meaningless operation, and as such it might be optimized away. Hence, this file must be compiled without optimizations (```-O0``` flag).

## Contributing

When extending the current fuzzer, use variable types whose width is consistent across systems where possible. This is necessary to retain corpus portability. For example, use ```uint64_t``` rather than ```unsigned long```.

