//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_LOG_HPP
#define SRS_PROTOCOL_LOG_HPP

#include <srs_core.hpp>

#include <map>
#include <string>

#include <srs_kernel_log.hpp>
#include <srs_protocol_st.hpp>

class ISrsRand;

// The context id of each st-thread identifies the client it serves.
class SrsThreadContext : public ISrsContext
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<srs_thread_t, SrsContextId> cache_;
    ISrsRand *rand_;

public:
    SrsThreadContext();
    virtual ~SrsThreadContext();

public:
    virtual SrsContextId generate_id();
    virtual const SrsContextId &get_id();
    virtual const SrsContextId &set_id(const SrsContextId &v);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void clear_cid();
};

extern const SrsContextId &srs_context_set_cid_of(srs_thread_t trd, const SrsContextId &v);

bool srs_log_header(char *buffer, int size, bool utc, bool dangerous, const char *tag, SrsContextId cid, const char *level, int *psize);

#endif
