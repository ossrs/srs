/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

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

#ifndef SRS_APP_LATEST_VERSION_HPP
#define SRS_APP_LATEST_VERSION_HPP

/*
#include <srs_app_latest_version.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_st.hpp>

#include <string>

class SrsLatestVersion : public ISrsCoroutineHandler
{
private:
    SrsCoroutine* trd_;
    std::string server_id_;
private:
    std::string match_version_;
    std::string stable_version_;
public:
    SrsLatestVersion();
    virtual ~SrsLatestVersion();
public:
    virtual srs_error_t start();
// interface ISrsEndlessThreadHandler.
public:
    virtual srs_error_t cycle();
private:
    srs_error_t query_latest_version(std::string& url);
};

#endif

