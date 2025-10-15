//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_PITHY_PRINT_HPP
#define SRS_KERNEL_PITHY_PRINT_HPP

#include <srs_core.hpp>

#include <map>

class ISrsConfig;

// The stage info to calc the age.
class SrsStageInfo
{
public:
    int stage_id_;
    srs_utime_t interval_;
    int nb_clients_;
    // The number of call of can_print().
    uint32_t nn_count_;
    // The ratio for interval, 1.0 means no change.
    double interval_ratio_;
    ISrsConfig *config_;

public:
    srs_utime_t age_;

public:
    SrsStageInfo(int _stage_id, double ratio = 1.0);
    virtual ~SrsStageInfo();
    virtual void update_print_time();

public:
    virtual void elapse(srs_utime_t diff);
    virtual bool can_print();
};

// The manager for stages, it's used for a single client stage.
// Of course, we can add the multiple user support, which is SrsPithyPrint.
class SrsStageManager
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<int, SrsStageInfo *> stages_;

public:
    SrsStageManager();
    virtual ~SrsStageManager();

public:
    // Fetch a stage, create one if not exists.
    SrsStageInfo *fetch_or_create(int stage_id, bool *pnew = NULL);
};

// The error pithy print is a single client stage manager, each stage only has one client.
// For example, we use it for error pithy print for each UDP packet processing.
class SrsErrorPithyPrint
{
public:
    // The number of call of can_print().
    uint32_t nn_count_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    double ratio_;
    SrsStageManager stages_;
    std::map<int, srs_utime_t> ticks_;

public:
    SrsErrorPithyPrint(double ratio = 1.0);
    virtual ~SrsErrorPithyPrint();

public:
    // Whether specified stage is ready for print.
    bool can_print(srs_error_t err, uint32_t *pnn = NULL);
    // We also support int error code.
    bool can_print(int err, uint32_t *pnn = NULL);
};

// An standalone pithy print, without shared stages.
class SrsAlonePithyPrint
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsStageInfo info_;
    srs_utime_t previous_tick_;

public:
    SrsAlonePithyPrint();
    virtual ~SrsAlonePithyPrint();

public:
    virtual void elapse();
    virtual bool can_print();
};

// The interface for pithy print.
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

// The stage is used for a collection of object to do print,
// the print time in a stage is constant and not changed,
// that is, we always got one message to print every specified time.
//
// For example, stage #1 for all play clients, print time is 3s,
// if there is 1client, it will print every 3s.
// if there is 10clients, random select one to print every 3s.
// Usage:
//        SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_rtmp_play());
//        while (true) {
//            pprint->elapse();
//            if (pprint->can_print()) {
//                // print pithy message.
//                // user can get the elapse time by: pprint->age()
//            }
//            // read and write RTMP messages.
//        }
class SrsPithyPrint : public ISrsPithyPrint
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int client_id_;
    SrsStageInfo *cache_;
    int stage_id_;
    srs_utime_t age_;
    srs_utime_t previous_tick_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsPithyPrint(int _stage_id);

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
    // For RTC sender and receiver, we create printer for each fd.
    static SrsPithyPrint *create_rtc_send(int fd);
    static SrsPithyPrint *create_rtc_recv(int fd);
    static SrsPithyPrint *create_srt_play();
    static SrsPithyPrint *create_srt_publish();
    virtual ~SrsPithyPrint();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Enter the specified stage, return the client id.
    virtual int
    enter_stage();
    // Leave the specified stage, release the client id.
    virtual void leave_stage();

public:
    // Auto calc the elapse time
    virtual void elapse();
    // Whether current client can print.
    virtual bool can_print();
    // Get the elapsed time in srs_utime_t.
    virtual srs_utime_t age();
};

// Global stage manager for pithy print, multiple stages.
extern SrsStageManager *_srs_stages;

#endif
