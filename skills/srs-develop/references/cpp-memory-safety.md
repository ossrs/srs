# C++ Memory Safety

The C++ server is one process: one memory bug takes down every stream, and a test can pass while it reads past a buffer. Check every C++ change against these rules, whoever writes or reviews it.

## Ownership

Every pointer has exactly one owner, and the code says which.

- **Raw pointer + `srs_freep`:** the destructor frees every owned member; free the old value before reassigning; a class that owns a raw pointer must not be copied. `srs_freepa` only for `new[]`, `free` only for `malloc`.
- **`SrsUniquePtr`:** use it for an owned local on a function with early returns. `SrsUniquePtr<T[]>` for arrays. A `get()` result must not outlive the owner, and must not go to code that frees it.
- **`SrsSharedPtr`:** build it from a raw pointer once, then copy the `SrsSharedPtr`; two built from one pointer free it twice. No cycles: there is no weak pointer. The count is not atomic: never share one with a non-ST thread, such as libsrt's.
- **`SrsSharedResource` and the resource manager:** `remove()` frees the object later, in the manager's coroutine. Keep the shared resource, not a raw pointer, for anything used after a yield.
- **`srs_error_t`:** a heap object. Return it or `srs_freep` it exactly once, also in loops and on paths that ignore the error.

## Coroutine switches

Another coroutine runs at every socket read or write, sleep, cond wait and HTTP hook, and at any call that can reach one. It can free objects and change containers.

- Do not use a pointer, reference or iterator taken before such a call after it. Look the object up again, or hold an `SrsSharedPtr`.
- Do not erase from a container while another coroutine may be iterating it.
- An object must not free itself, or be freed by its handler, while one of its methods is still running.

## Input

Every byte from a client, an origin or a config file is hostile.

- `require()` or `require_bits()` before every `SrsBuffer` or `SrsBitBuffer` read or skip. Never `srs_assert` on input: it aborts the server.
- Check a length from input before using it in `memcpy`, an index or size arithmetic; watch overflow and signed and unsigned mixes.
- Check a container is not empty before `[0]`, `front()` or `back()`.
- Cap every buffer, queue or container that input makes grow, and every recursion depth input controls: coroutine stacks are small.

## Initialization

Set every pointer and scalar member in every constructor.

## Proving it

- The utest build has `--sanitizer=on`: on macOS it reports use-after-free, double free and overflow, but not leaks.
- Leaks need LeakSanitizer on Linux: `bash skills/srs-develop/scripts/srs-utest-asan-linux.sh '<Suite>.*'`. Its baseline still has leaks the tests make, so compare the reports in the code you changed.
- The test must drive the path: the error return, the malformed input, the close or removal during a yield.
- Tests free what they build, so a real leak stands out.
