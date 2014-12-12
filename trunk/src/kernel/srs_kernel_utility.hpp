/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_KERNEL_UTILITY_HPP
#define SRS_KERNEL_UTILITY_HPP

/*
#include <srs_kernel_utility.hpp>
*/

#include <srs_core.hpp>

#include <string>

// compare
#define srs_min(a, b) (((a) < (b))? (a) : (b))
#define srs_max(a, b) (((a) < (b))? (b) : (a))

// get current system time in ms, use cache to avoid performance problem
extern int64_t srs_get_system_time_ms();
extern int64_t srs_get_system_startup_time_ms();
// the deamon st-thread will update it.
extern int64_t srs_update_system_time_ms();

// dns resolve utility, return the resolved ip address.
extern std::string srs_dns_resolve(std::string host);

// whether system is little endian
extern bool srs_is_little_endian();

// replace old_str to new_str of str
extern std::string srs_string_replace(std::string str, std::string old_str, std::string new_str);
// trim char in trim_chars of str
extern std::string srs_string_trim_end(std::string str, std::string trim_chars);
// trim char in trim_chars of str
extern std::string srs_string_trim_start(std::string str, std::string trim_chars);
// remove char in remove_chars of str
extern std::string srs_string_remove(std::string str, std::string remove_chars);
// whether string end with
extern bool srs_string_ends_with(std::string str, std::string flag);

#endif

