//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI39_HPP
#define SRS_UTEST_AI39_HPP

/*
#include <srs_utest_ai39.hpp>
*/
#include <srs_utest.hpp>

#include <string>
#include <vector>

// Builds a bitstream field by field, most significant bit first, for the H.264 and H.265 parameter set parsers.
class UtestBitWriter
{
public:
    // One 0/1 entry per bit.
    std::vector<uint8_t> bits_;

public:
    // The low n bits of v, n at most 32.
    void u(uint32_t v, int n);
    // ue(v), 9.2 of ITU-T H.265 and 9.1 of ISO_IEC_14496-10.
    void ue(uint32_t v);
    // se(v), codeNum k maps to (-1)^(k+1) * Ceil(k / 2).
    void se(int32_t v);
    int size() const;
    // The first n bits, zero padded to a byte boundary.
    std::vector<char> bytes(int n) const;
    std::vector<char> bytes() const;
};

#endif
