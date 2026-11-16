# C++ Code Style

For memory, follow `references/cpp-memory-safety.md`: ownership, coroutine switches, input bounds and the sanitizer runs.

## Iterator

Never use `it->first` or `it->second` directly. Declare a variable first:

```cpp
SrsRtcTrackDescription *track = it->second;
srs_freep(track);
```

## Void Call

When calling a function that returns `void`, not `srs_error_t`, add `// void` so a reviewer does not look for the missing error handling:

```cpp
drain_client_input(); // void
```

## Platform Calls

The code builds on Linux, macOS and Windows. Call OS functions through the `srs_` shims in `trunk/src/core/srs_core_platform.hpp`, and add a shim there for a new OS difference, so other code has no `#ifdef` per OS.
