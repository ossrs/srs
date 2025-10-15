//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_BALANCE_HPP
#define SRS_KERNEL_BALANCE_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

// Interface for round-robin load balance algorithm.
//
// This interface defines the contract for load balancing algorithms that distribute
// requests across multiple servers in a round-robin fashion. It's primarily used
// for edge pull scenarios and other features that require distributing load across
// multiple backend servers.
//
// The round-robin algorithm ensures fair distribution by cycling through available
// servers in sequence, giving each server an equal opportunity to handle requests.
//
class ISrsLbRoundRobin
{
public:
    ISrsLbRoundRobin();
    virtual ~ISrsLbRoundRobin();

public:
    // Select one server from the provided list of servers using the load balancing algorithm.
    //
    // @param servers A vector of server addresses/URLs to choose from. Must not be empty.
    // @return The selected server address/URL as a string.
    //
    // @remark The implementation should handle the load balancing logic and maintain
    //         any necessary state to ensure proper distribution across servers.
    // @remark Callers must ensure the servers vector is not empty before calling this method.
    //
    virtual std::string select(const std::vector<std::string> &servers) = 0;
};

// Implementation of round-robin load balance algorithm.
//
// This class provides a concrete implementation of the ISrsLbRoundRobin interface
// that distributes requests across multiple servers using a simple round-robin
// algorithm. It maintains internal state to track the current position in the
// server list and ensures fair distribution by cycling through servers sequentially.
//
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
    // Get the current server index.
    virtual uint32_t current();
    // Get the currently selected server.
    virtual std::string selected();
    // Select the next server using round-robin algorithm.
    virtual std::string select(const std::vector<std::string> &servers);
};

#endif
