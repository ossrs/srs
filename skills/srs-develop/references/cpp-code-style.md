# C++ Code Style

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

The same sources build on Linux and macOS with `./configure`, and on Windows x64 with MSVC as C++14. Keep OS differences in the platform shims of `trunk/src/core/srs_core_platform.hpp` and `.cpp`, so app code has no `#ifdef` per OS:

- Call the shim, not the POSIX function, where one exists, such as `srs_close_socket`, `srs_setsockopt`, `srs_bind`, `srs_file_open`, `srs_rename`, `srs_gettimeofday`, `srs_localtime_r`, `srs_random`, `srs_kill` or `srs_process_spawn`. Use `srs_socket_t` for a socket outside ST and `srs_off_t` for a file offset.
- A new OS difference gets a new shim: a function with an `srs_` prefix, not a `#define` of the POSIX name. On Unix it is a one-line call of the POSIX function; the Windows code goes under `_WIN32`.
- Winsock does not set `errno`. A shim whose caller reads `errno` sets it from `WSAGetLastError`, as `srs_bind` does.
- Do not add POSIX headers to a source file: `srs_core_platform.hpp` includes the per-OS headers.
- A new source file gets its line in `trunk/auto/module_files.txt`, which both builds read.
- A utest that has no meaning on Windows, such as one that forks or raises a signal, is guarded with `#ifndef _WIN32 // POSIX only: <reason>`. A test with a Windows meaning states it under `#ifdef _WIN32` instead.
