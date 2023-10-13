#
# SRT - Secure, Reliable, Transport Copyright (c) 2022 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public License,
# v. 2.0. If a copy of the MPL was not distributed with this file, You can
# obtain one at http://mozilla.org/MPL/2.0/.
#

# Check for C++11 std::put_time().
#
# Sets:
#   HAVE_CXX_STD_PUT_TIME

include(CheckCSourceCompiles)

function(CheckCXXStdPutTime)

   unset(HAVE_CXX_STD_PUT_TIME CACHE)

   set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE) # CMake 3.6

   unset(CMAKE_REQUIRED_FLAGS)
   unset(CMAKE_REQUIRED_LIBRARIES)
   unset(CMAKE_REQUIRED_LINK_OPTIONS)

   set(CheckCXXStdPutTime_CODE
      "
      #include <iostream>
      #include <iomanip>
      #include <ctime>
      int main(void)
      {
         const int result = 0;
         std::time_t t = std::time(nullptr);
         std::tm tm = *std::localtime(&t);
         std::cout
            << std::put_time(&tm, \"%FT%T\")
            << std::setfill('0')
            << std::setw(6)
            << std::endl;
         return result;
      }
      "
   )

   # NOTE: Should we set -std or use the current compiler configuration.
   #     It seems that the top level build does not track the compiler
   #     in a consistent manner. So Maybe we need this?
   set(CMAKE_REQUIRED_FLAGS "-std=c++11")

   # Check that the compiler can build the std::put_time() example:
   message(STATUS "Checking for C++ 'std::put_time()':")
   check_cxx_source_compiles(
      "${CheckCXXStdPutTime_CODE}"
      HAVE_CXX_STD_PUT_TIME)

endfunction(CheckCXXStdPutTime)
