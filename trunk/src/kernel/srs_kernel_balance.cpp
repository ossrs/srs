//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_balance.hpp>

#include <srs_kernel_error.hpp>

using namespace std;

ISrsLbRoundRobin::ISrsLbRoundRobin()
{
}

ISrsLbRoundRobin::~ISrsLbRoundRobin()
{
}

SrsLbRoundRobin::SrsLbRoundRobin()
{
    index_ = -1;
    count_ = 0;
}

SrsLbRoundRobin::~SrsLbRoundRobin()
{
}

uint32_t SrsLbRoundRobin::current()
{
    return index_;
}

string SrsLbRoundRobin::selected()
{
    return elem_;
}

string SrsLbRoundRobin::select(const vector<string> &servers)
{
    srs_assert(!servers.empty());

    index_ = (int)(count_++ % servers.size());
    elem_ = servers.at(index_);

    return elem_;
}
