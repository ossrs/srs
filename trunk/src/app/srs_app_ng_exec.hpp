/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
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

#ifndef SRS_APP_NG_EXEC_HPP
#define SRS_APP_NG_EXEC_HPP

#include <srs_core.hpp>

#include <vector>
#include <string>

#include <srs_app_thread.hpp>

class SrsRequest;
class SrsPithyPrint;
class SrsProcess;

/**
 * the ng-exec is the exec feature introduced by nginx-rtmp,
 * @see https://github.com/arut/nginx-rtmp-module/wiki/Directives#exec_push
 * @see https://github.com/ossrs/srs/issues/367
 */
class SrsNgExec : public ISrsCoroutineHandler
{
private:
    SrsCoroutine* trd;
    SrsPithyPrint* pprint;
    std::string input_stream_name;
    std::vector<SrsProcess*> exec_publishs;
public:
    SrsNgExec();
    virtual ~SrsNgExec();
public:
    virtual srs_error_t on_publish(SrsRequest* req);
    virtual void on_unpublish();
// interface ISrsReusableThreadHandler.
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
private:
    virtual srs_error_t parse_exec_publish(SrsRequest* req);
    virtual void clear_exec_publish();
    virtual void show_exec_log_message();
    virtual std::string parse(SrsRequest* req, std::string tmpl);
};

#endif

