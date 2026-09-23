//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI32_HPP
#define SRS_UTEST_AI32_HPP

/*
#include <srs_utest_ai32.hpp>
*/
#include <srs_utest.hpp>

#include <srs_kernel_factory.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_pithy_print.hpp>

#include <map>
#include <string>
#include <vector>

// Records every stage lookup a pithy print asks for, and owns the stages it hands back.
class MockStageManagerForPithyPrint : public ISrsStageManager
{
public:
    // The stage ids fetch_or_create() was asked for, in order.
    std::vector<int> fetch_ids_;
    // The stages handed out, keyed by stage id, owned by this mock.
    std::map<int, SrsStageInfo *> stages_;

public:
    MockStageManagerForPithyPrint();
    virtual ~MockStageManagerForPithyPrint();

public:
    virtual SrsStageInfo *fetch_or_create(int stage_id, bool *pnew = NULL);
};

// A config carrying the print interval the test chose.
class MockConfigForStageInfo : public ISrsConfig
{
public:
    srs_utime_t pithy_print_;

public:
    MockConfigForStageInfo();
    virtual ~MockConfigForStageInfo();

public:
    virtual srs_utime_t get_pithy_print();
    virtual std::string get_default_app_name();
    virtual std::string get_srt_default_mode();
};

// Hands out MockConfigForStageInfo, and counts how many a stage asked for.
// MockKernelFactoryForFastTimer cannot serve here, because its create_config() returns NULL.
class MockKernelFactoryForStageInfo : public ISrsKernelFactory
{
public:
    // The interval every config this factory creates reports.
    srs_utime_t pithy_print_;
    // How many configs this factory was asked for.
    int create_config_count_;

public:
    MockKernelFactoryForStageInfo();
    virtual ~MockKernelFactoryForStageInfo();

public:
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid);
    virtual ISrsTime *create_time();
    virtual ISrsConfig *create_config();
    virtual ISrsCond *create_cond();
};

// A clock the test moves by hand.
class MockClockForPithyPrint : public ISrsClock
{
public:
    srs_utime_t now_;

public:
    MockClockForPithyPrint();
    virtual ~MockClockForPithyPrint();

public:
    virtual srs_utime_t now();
};

#endif
