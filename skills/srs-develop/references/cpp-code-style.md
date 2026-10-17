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
