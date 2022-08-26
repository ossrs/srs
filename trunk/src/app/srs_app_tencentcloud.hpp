//
// Copyright (c) 2013-2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_TENCENTCLOUD_HPP
#define SRS_APP_TENCENTCLOUD_HPP

#include <srs_core.hpp>

#include <srs_kernel_buffer.hpp>

#include <string>
#include <vector>

class SrsBuffer;
class SrsClsLogGroupList;
class SrsClsLogGroup;
class SrsClsLog;

class SrsClsSugar : public ISrsEncoder
{
private:
    SrsClsLog* log_;
    SrsClsLogGroup* log_group_;
    SrsClsLogGroupList* log_groups_;
public:
    SrsClsSugar();
    virtual ~SrsClsSugar();
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
public:
    SrsClsSugar* kv(std::string k, std::string v);
    SrsClsSugar* kvf(std::string k, const char* fmt, ...);
};

class SrsClsSugars : public ISrsEncoder
{
private:
    std::vector<SrsClsSugar*> sugars;
public:
    SrsClsSugars();
    virtual ~SrsClsSugars();
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
public:
    SrsClsSugar* create();
    SrsClsSugars* slice(int max_size);
    bool empty();
    int size();
};

class SrsClsClient
{
private:
    bool enabled_;
    bool stat_heartbeat_;
    bool stat_streams_;
    bool debug_logging_;
    int heartbeat_ratio_;
    int streams_ratio_;
    std::string label_;
    std::string tag_;
private:
    std::string secret_id_;
    std::string endpoint_;
    std::string topic_;
private:
    uint64_t nn_logs_;
public:
    SrsClsClient();
    virtual ~SrsClsClient();
public:
    bool enabled();
    bool stat_heartbeat();
    bool stat_streams();
    int heartbeat_ratio();
    int streams_ratio();
    std::string label();
    std::string tag();
    uint64_t nn_logs();
public:
    srs_error_t initialize();
private:
    srs_error_t send_log(ISrsEncoder* sugar, int count, int total);
public:
    srs_error_t send_logs(SrsClsSugars* sugars);
};

extern SrsClsClient* _srs_cls;

srs_error_t srs_cls_report();

#endif

