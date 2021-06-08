//
// Copyright (c) 2013-2021 Runner365
//
// SPDX-License-Identifier: MIT
//

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
