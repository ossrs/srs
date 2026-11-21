//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_PITHY_PRINT_HPP
#define SRS_KERNEL_PITHY_PRINT_HPP

#include <srs_core.hpp>

#include <map>

class ISrsClock;
class ISrsConfig;
class ISrsKernelFactory;

class SrsStageInfo
{
public:
    int stage_id_;
    srs_utime_t interval_;
    int nb_clients_;
    uint32_t nn_count_;
    double interval_ratio_;
    ISrsConfig *config_;
    ISrsKernelFactory *factory_;

public:
    srs_utime_t age_;

public:
    SrsStageInfo(int stage_id, double ratio = 1.0);
    virtual ~SrsStageInfo();
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual void update_print_time();

public:
    virtual void elapse(srs_utime_t diff);
    virtual bool can_print();
};

class ISrsStageManager
{
public:
    ISrsStageManager();
    virtual ~ISrsStageManager();

public:
    virtual SrsStageInfo *fetch_or_create(int stage_id, bool *pnew = NULL) = 0;
};

class SrsStageManager : public ISrsStageManager
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<int, SrsStageInfo *> stages_;

public:
    SrsStageManager();
    virtual ~SrsStageManager();

public:
    virtual SrsStageInfo *fetch_or_create(int stage_id, bool *pnew = NULL);
};

class SrsErrorPithyPrint
{
public:
    uint32_t nn_count_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    double ratio_;
    SrsStageManager stages_;
    std::map<int, srs_utime_t> ticks_;
    ISrsClock *clk_;

public:
    SrsErrorPithyPrint(double ratio = 1.0);
    virtual ~SrsErrorPithyPrint();

public:
    bool can_print(srs_error_t err, uint32_t *pnn = NULL);
    bool can_print(int err, uint32_t *pnn = NULL);
};

class SrsAlonePithyPrint
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsStageInfo info_;
    srs_utime_t previous_tick_;
    ISrsClock *clk_;

public:
    SrsAlonePithyPrint();
    virtual ~SrsAlonePithyPrint();
    void assemble(); // Creates the members, to avoid calling functions in the constructor.

public:
    virtual void elapse();
    virtual bool can_print();
};

class ISrsPithyPrint
{
public:
    ISrsPithyPrint();
    virtual ~ISrsPithyPrint();

public:
    virtual void elapse() = 0;
    virtual bool can_print() = 0;
    virtual srs_utime_t age() = 0;
};

// The clients of a stage share its print time: with 1 or 10 play clients and a 3s
// print time, one message prints every 3s, from one of the clients.
class SrsPithyPrint : public ISrsPithyPrint
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int client_id_;
    SrsStageInfo *cache_;
    int stage_id_;
    srs_utime_t age_;
    srs_utime_t previous_tick_;
    ISrsStageManager *stages_;
    ISrsClock *clk_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsPithyPrint(int stage_id);
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    static SrsPithyPrint *create(int stage_id);

public:
    static SrsPithyPrint *create_rtmp_play();
    static SrsPithyPrint *create_rtmp_publish();
    static SrsPithyPrint *create_hls();
    static SrsPithyPrint *create_forwarder();
    static SrsPithyPrint *create_encoder();
    static SrsPithyPrint *create_exec();
    static SrsPithyPrint *create_ingester();
    static SrsPithyPrint *create_edge();
    static SrsPithyPrint *create_caster();
    static SrsPithyPrint *create_http_stream();
    static SrsPithyPrint *create_http_stream_cache();
    static SrsPithyPrint *create_rtc_play();
    static SrsPithyPrint *create_rtc_send(int fd);
    static SrsPithyPrint *create_rtc_recv(int fd);
    static SrsPithyPrint *create_srt_play();
    static SrsPithyPrint *create_srt_publish();
    virtual ~SrsPithyPrint();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual int
    enter_stage();
    virtual void leave_stage();

public:
    virtual void elapse();
    virtual bool can_print();
    virtual srs_utime_t age();
};

extern SrsStageManager *_srs_stages;

#endif
