//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI42_HPP
#define SRS_UTEST_AI42_HPP

/*
#include <srs_utest_ai42.hpp>
*/
#include <srs_utest.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_pithy_print.hpp>

// The pithy print stages, with one stage set to print at each call, so its line, such as the kbps line of an edge pull
// or push, is logged at once, not after the configured 10s. Objects of the stage must be made inside the scope.
class PithyPrintAtOnceScope
{
public:
    SrsStageManager stages_;
    SrsStageManager *saved_;

public:
    PithyPrintAtOnceScope(SrsPithyPrint *(*create_stage)())
    {
        saved_ = _srs_stages;
        _srs_stages = &stages_;

        SrsUniquePtr<SrsPithyPrint> pprint(create_stage());
        pprint->elapse();
        pprint->cache_->interval_ = 0;
    }
    ~PithyPrintAtOnceScope()
    {
        _srs_stages = saved_;
    }
};

#endif
