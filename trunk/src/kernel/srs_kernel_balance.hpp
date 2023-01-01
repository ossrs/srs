//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_KERNEL_BALANCE_HPP
#define SRS_KERNEL_BALANCE_HPP

#include <srs_core.hpp>

#include <vector>
#include <string>

/**
 * the round-robin load balance algorithm,
 * used for edge pull and other multiple server feature.
 */
class SrsLbRoundRobin
{
private:
    // current selected index.
    int index;
    // total scheduled count.
    uint32_t count;
    // current selected server.
    std::string elem;
public:
    SrsLbRoundRobin();
    virtual ~SrsLbRoundRobin();
public:
    virtual uint32_t current();
    virtual std::string selected();
    virtual std::string select(const std::vector<std::string>& servers);
};

#endif

