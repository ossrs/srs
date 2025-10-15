//
// Copyright (c) 2013-2025 The SRS Authors
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

// The st thread context, get_id will get the st-thread id,
// which identify the client.
class SrsThreadContext : public ISrsContext
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<srs_thread_t, SrsContextId> cache_;

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

// Set the context id of specified thread, not self.
extern const SrsContextId &srs_context_set_cid_of(srs_thread_t trd, const SrsContextId &v);

// Generate the log header.
// @param dangerous Whether log is warning or error, log the errno if true.
// @param utc Whether use UTC time format in the log header.
// @param psize Output the actual header size.
// @remark It's a internal API.
bool srs_log_header(char *buffer, int size, bool utc, bool dangerous, const char *tag, SrsContextId cid, const char *level, int *psize);

#endif
