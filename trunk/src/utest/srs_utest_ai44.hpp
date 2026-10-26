//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI44_HPP
#define SRS_UTEST_AI44_HPP

/*
#include <srs_utest_ai44.hpp>
*/
#include <srs_utest.hpp>

#include <srs_kernel_log.hpp>
#include <srs_utest_ai15.hpp>

#include <string>

// The server's log while it lives: each warning and trace is kept.
class ListenerUtestLog
{
public:
    MockLogForSignal log_;
    ISrsLog *saved_;

public:
    ListenerUtestLog()
    {
        saved_ = _srs_log;
        _srs_log = &log_;
    }
    ~ListenerUtestLog()
    {
        _srs_log = saved_;
    }

public:
    // The last trace that holds the text, or "".
    std::string last_trace(std::string text)
    {
        std::string found;
        for (int i = 0; i < (int)log_.traces_.size(); i++) {
            if (log_.traces_[i].find(text) != std::string::npos) {
                found = log_.traces_[i];
            }
        }
        return found;
    }
    // How many warnings hold the text.
    int warns(std::string text)
    {
        int n = 0;
        for (int i = 0; i < (int)log_.warns_.size(); i++) {
            if (log_.warns_[i].find(text) != std::string::npos) {
                n++;
            }
        }
        return n;
    }
};

#endif
