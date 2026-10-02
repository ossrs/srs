//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI06_HPP
#define SRS_UTEST_AI06_HPP

/*
#include <srs_utest_ai06.hpp>
*/
#include <srs_utest.hpp>

#include <srs_kernel_utility.hpp>

#include <string>
#include <vector>

// Mock random generator, fills each gen_bytes() call with one distinct byte,
// 0x10 for the first call, 0x11 for the second, and so on.
// Each gen_str() call returns the next of gen_str_values_, then a string of x.
class MockRandForHandshake : public ISrsRand
{
public:
    long integer_value_;
    int integer_count_;
    std::vector<int> gen_bytes_sizes_;
    std::vector<int> gen_str_lens_;
    std::vector<std::string> gen_str_values_;

public:
    MockRandForHandshake();
    virtual ~MockRandForHandshake();

public:
    virtual void gen_bytes(char *bytes, int size);
    virtual std::string gen_str(int len);
    virtual long integer();
    virtual long integer(long min, long max);
};

#endif
