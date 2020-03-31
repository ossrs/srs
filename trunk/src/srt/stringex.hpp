/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Runner365
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef STRING_EX_H
#define STRING_EX_H

#include <srs_core.hpp>

#include <iostream>
#include <string.h>
#include <vector>
#include <algorithm>
#include <iterator>
#include <cctype>

inline int string_split(const std::string& input_str, const std::string& split_str, std::vector<std::string>& output_vec) {
    if (input_str.length() == 0) {
        return 0;
    }
    
    std::string tempString(input_str);
    do {

        size_t pos = tempString.find(split_str);
        if (pos == tempString.npos) {
            output_vec.push_back(tempString);
            break;
        }
        std::string seg_str = tempString.substr(0, pos);
        tempString = tempString.substr(pos+split_str.size());
        output_vec.push_back(seg_str);
    } while(tempString.size() > 0);

    return output_vec.size();
}

inline std::string string_lower(const std::string input_str) {
    std::string output_str(input_str);

    std::transform(input_str.begin(), input_str.end(), output_str.begin(), ::tolower);

    return output_str;
}

#endif//STRING_EX_H