//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef TIME_HELP_H
#define TIME_HELP_H

#include <srs_core.hpp>

#include <chrono>

inline long long now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}

#endif //TIME_HELP_H