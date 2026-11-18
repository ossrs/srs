//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_BALANCE_HPP
#define SRS_KERNEL_BALANCE_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class ISrsLbRoundRobin
{
public:
    ISrsLbRoundRobin();
    virtual ~ISrsLbRoundRobin();

public:
    virtual std::string select(const std::vector<std::string> &servers) = 0;
};

class SrsLbRoundRobin : public ISrsLbRoundRobin
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int index_;
    uint32_t count_;
    std::string elem_;

public:
    SrsLbRoundRobin();
    virtual ~SrsLbRoundRobin();

public:
    virtual uint32_t current();
    virtual std::string selected();
    virtual std::string select(const std::vector<std::string> &servers);
};

#endif
