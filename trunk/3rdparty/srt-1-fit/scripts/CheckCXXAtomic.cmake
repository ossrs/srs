#
# SRT - Secure, Reliable, Transport
# Copyright (c) 2021 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

# Check for c++11 std::atomic.
#
# Sets:
#     HAVE_CXX_ATOMIC
#     HAVE_CXX_ATOMIC_STATIC

include(CheckCXXSourceCompiles)
include(CheckLibraryExists)

function(CheckCXXAtomic)

   unset(HAVE_CXX_ATOMIC CACHE)
   unset(HAVE_CXX_ATOMIC_STATIC CACHE)

   unset(CMAKE_REQUIRED_FLAGS)
   unset(CMAKE_REQUIRED_LIBRARIES)
   unset(CMAKE_REQUIRED_LINK_OPTIONS)

   set(CheckCXXAtomic_CODE
      "
      #include<cstdint>
      #include<atomic>
      int main(void)
      {
         std::atomic<std::ptrdiff_t> x(0);
         std::atomic<std::intmax_t> y(0);
         return x + y;
      }
      ")

   set(CMAKE_REQUIRED_FLAGS "-std=c++11")

   check_cxx_source_compiles(
      "${CheckCXXAtomic_CODE}"
      HAVE_CXX_ATOMIC)

   if(HAVE_CXX_ATOMIC)
      # CMAKE_REQUIRED_LINK_OPTIONS was introduced in CMake 3.14.
      if(CMAKE_VERSION VERSION_LESS "3.14")
         set(CMAKE_REQUIRED_LINK_OPTIONS "-static")
      else()
         set(CMAKE_REQUIRED_FLAGS "-std=c++11 -static")
      endif()
      check_cxx_source_compiles(
         "${CheckCXXAtomic_CODE}"
         HAVE_CXX_ATOMIC_STATIC)
   endif()

   unset(CMAKE_REQUIRED_FLAGS)
   unset(CMAKE_REQUIRED_LIBRARIES)
   unset(CMAKE_REQUIRED_LINK_OPTIONS)

endfunction(CheckCXXAtomic)
