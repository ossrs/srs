//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_kernel_balance.hpp>

#include <srs_kernel_error.hpp>

using namespace std;

SrsLbRoundRobin::SrsLbRoundRobin()
{
    index = -1;
    count = 0;
}

SrsLbRoundRobin::~SrsLbRoundRobin()
{
}

uint32_t SrsLbRoundRobin::current()
{
    return index;
}

string SrsLbRoundRobin::selected()
{
    return elem;
}

string SrsLbRoundRobin::select(const vector<string>& servers)
{
    srs_assert(!servers.empty());
    
    index = (int)(count++ % servers.size());
    elem = servers.at(index);
    
    return elem;
}

