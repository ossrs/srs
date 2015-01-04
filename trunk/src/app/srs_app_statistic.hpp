/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#ifndef SRS_APP_STATISTIC_HPP
#define SRS_APP_STATISTIC_HPP

/*
#include <srs_app_statistic.hpp>
*/

#include <srs_core.hpp>

#include <map>

class SrsRequest;

class SrsStreamInfo
{
public:
    SrsStreamInfo();
    virtual ~SrsStreamInfo();

    SrsRequest *_req;
};
typedef std::map<void*, SrsStreamInfo*> SrsStreamInfoMap;

class SrsStatistic
{
public:
    static SrsStatistic *instance()
    {
        if (_instance == NULL) {
            _instance = new SrsStatistic();
        }
        return _instance;
    }

    virtual SrsStreamInfoMap* get_pool();

    virtual void add_request_info(void *p, SrsRequest *req);
    
private:
    SrsStatistic();
    virtual ~SrsStatistic();
    static SrsStatistic *_instance;
    SrsStreamInfoMap pool;
    virtual SrsStreamInfo *get(void *p);
};

#endif