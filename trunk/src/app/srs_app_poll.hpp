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

#ifndef SRS_APP_POLL_HPP
#define SRS_APP_POLL_HPP

/*
#include <srs_app_poll.hpp>
*/

#include <srs_core.hpp>

#include <map>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>

class SrsPollFD;

/**
* the poll for all play clients to finger the active fd out.
* for performance issue, @see: https://github.com/winlinvip/simple-rtmp-server/issues/194
* the poll is shared by all SrsPollFD, and we start an isolate thread to finger the active fds.
*/
class SrsPoll : public ISrsThreadHandler
{
private:
    SrsThread* pthread;
    std::map<st_netfd_t, SrsPollFD*> fds;
public:
    SrsPoll();
    virtual ~SrsPoll();
public:
    /**
    * start the poll thread.
    */
    virtual int start();
    /**
    * start an cycle thread.
    */
    virtual int cycle();
// singleton
private:
    static SrsPoll* _instance;
public:
    static SrsPoll* instance();
};

/**
* the poll fd to check whether the specified fd is active.
*/
class SrsPollFD
{
private:
    st_netfd_t _stfd;
public:
    SrsPollFD();
    virtual ~SrsPollFD();
public:
    /**
    * initialize the poll.
    * @param stfd the fd to poll.
    */
    virtual int initialize(st_netfd_t stfd);
};

#endif

