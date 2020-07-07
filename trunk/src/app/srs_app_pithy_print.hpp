/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_PITHY_PRINT_HPP
#define SRS_APP_PITHY_PRINT_HPP

#include <srs_core.hpp>

#include <srs_app_reload.hpp>

// The stage info to calc the age.
class SrsStageInfo : public ISrsReloadHandler
{
public:
    int stage_id;
    srs_utime_t interval;
    int nb_clients;
public:
    srs_utime_t age;
public:
    SrsStageInfo(int _stage_id);
    virtual ~SrsStageInfo();
    virtual void update_print_time();
public:
    virtual void elapse(srs_utime_t diff);
    virtual bool can_print();
public:
    virtual srs_error_t on_reload_pithy_print();
};

// The stage is used for a collection of object to do print,
// the print time in a stage is constant and not changed,
// that is, we always got one message to print every specified time.
//
// For example, stage #1 for all play clients, print time is 3s,
// if there is 1client, it will print every 3s.
// if there is 10clients, random select one to print every 3s.
// Usage:
//        SrsPithyPrint* pprint = SrsPithyPrint::create_rtmp_play();
//        SrsAutoFree(SrsPithyPrint, pprint);
//        while (true) {
//            pprint->elapse();
//            if (pprint->can_print()) {
//                // print pithy message.
//                // user can get the elapse time by: pprint->age()
//            }
//            // read and write RTMP messages.
//        }
class SrsPithyPrint
{
private:
    int client_id;
    int stage_id;
    srs_utime_t _age;
    srs_utime_t previous_tick;
private:
    SrsPithyPrint(int _stage_id);
public:
    static SrsPithyPrint* create_rtmp_play();
    static SrsPithyPrint* create_rtmp_publish();
    static SrsPithyPrint* create_hls();
    static SrsPithyPrint* create_forwarder();
    static SrsPithyPrint* create_encoder();
    static SrsPithyPrint* create_exec();
    static SrsPithyPrint* create_ingester();
    static SrsPithyPrint* create_edge();
    static SrsPithyPrint* create_caster();
    static SrsPithyPrint* create_http_stream();
    static SrsPithyPrint* create_http_stream_cache();
    static SrsPithyPrint* create_rtc_play();
    // For RTC sender and receiver, we create printer for each fd.
    static SrsPithyPrint* create_rtc_send(int fd);
    static SrsPithyPrint* create_rtc_recv(int fd);
    virtual ~SrsPithyPrint();
private:
    // Enter the specified stage, return the client id.
    virtual int enter_stage();
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

#endif
