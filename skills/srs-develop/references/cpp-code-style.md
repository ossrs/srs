# C++ Code Style

## Iterator

Never use `it->first` or `it->second` directly. Declare a variable first:

```cpp
SrsRtcTrackDescription *track = it->second;
srs_freep(track);
```
