/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_APP_PITHY_PRINT_HPP
#define SRS_APP_PITHY_PRINT_HPP

/*
#include <srs_app_pithy_print.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_reload.hpp>

// the pithy stage for all play clients.
#define SRS_STAGE_PLAY_USER 1
// the pithy stage for all publish clients.
#define SRS_STAGE_PUBLISH_USER 2
// the pithy stage for all forward clients.
#define SRS_STAGE_FORWARDER 3
// the pithy stage for all encoders.
#define SRS_STAGE_ENCODER 4
// the pithy stage for all hls.
#define SRS_STAGE_HLS 5
// the pithy stage for all ingesters.
#define SRS_STAGE_INGESTER 6
// the pithy stage for all edge.
#define SRS_STAGE_EDGE 7

class SrsStageInfo : public ISrsReloadHandler
{
public:
    int stage_id;
    int pithy_print_time_ms;
    int nb_clients;
public:
    int64_t _age;
    int64_t printed_age;
public:
    SrsStageInfo(int _stage_id);
    virtual ~SrsStageInfo();
    virtual void update_print_time();
public:
    virtual void elapse(int64_t diff);
    virtual bool can_print();
public:
    virtual int on_reload_pithy_print();
};

/**
* the stage is used for a collection of object to do print,
* the print time in a stage is constant and not changed.
* for example, stage #1 for all play clients, print time is 3s,
* if there is 10clients, then all clients should print in 10*3s.
*/
class SrsPithyPrint
{
private:
    int client_id;
    int stage_id;
    // in ms.
    int64_t _age;
    int64_t previous_tick;
public:
    /**
    * @param _stage_id defined in SRS_STAGE_xxx, eg. SRS_STAGE_PLAY_USER.
    */
    SrsPithyPrint(int _stage_id);
    virtual ~SrsPithyPrint();
private:
    /**
    * enter the specified stage, return the client id.
    */
    virtual int enter_stage();
    /**
    * leave the specified stage, release the client id.
    */
    virtual void leave_stage();
public:
    /**
    * auto calc the elapse time
    */
    virtual void elapse();
    /**
    * whether current client can print.
    */
    virtual bool can_print();
    /**
    * get the elapsed time in ms.
    */
    virtual int64_t age();
};

#endif