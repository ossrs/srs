#ifndef TIME_HELP_H
#define TIME_HELP_H
#include <chrono>

inline long long now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}

#endif //TIME_HELP_H