/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#ifndef SRS_APP_PROCESS_HPP
#define SRS_APP_PROCESS_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

/**
 * to start and stop a process, cycle to restart the process when terminated.
 * the usage:
 *      // the binary is the process to fork.
 *      binary = "./objs/ffmpeg/bin/ffmpeg";
 *      // where argv is a array contains each params.
 *      argv = ["./objs/ffmpeg/bin/ffmpeg", "-i", "in.flv", "1", ">", "/dev/null", "2", ">", "/dev/null"];
 *
 *      process = new SrsProcess();
 *      if ((ret = process->initialize(binary, argv)) != ERROR_SUCCESS) { return ret; }
 *      if ((ret = process->start()) != ERROR_SUCCESS) { return ret; }
 *      if ((ret = process->cycle()) != ERROR_SUCCESS) { return ret; }
 *      process->fast_stop();
 *      process->stop();
 */
class SrsProcess
{
private:
    bool is_started;
    // whether SIGTERM send but need to wait or SIGKILL.
    bool fast_stopped;
    pid_t pid;
private:
    std::string bin;
    std::string stdout_file;
    std::string stderr_file;
    std::vector<std::string> params;
    // the cli to fork process.
    std::string cli;
    std::string actual_cli;
public:
    SrsProcess();
    virtual ~SrsProcess();
public:
    /**
     * get pid of process.
     */
    virtual int get_pid();
    /**
     * whether process is already started.
     */
    virtual bool started();
    /**
     * initialize the process with binary and argv.
     * @param binary the binary path to exec.
     * @param argv the argv for binary path, the argv[0] generally is the binary.
     * @remark the argv[0] must be the binary.
     */
    virtual srs_error_t initialize(std::string binary, std::vector<std::string> argv);
public:
    /**
     * start the process, ignore when already started.
     */
    virtual srs_error_t start();
    /**
     * cycle check the process, update the state of process.
     * @remark when process terminated(not started), user can restart it again by start().
     */
    virtual srs_error_t cycle();
    /**
     * send SIGTERM then SIGKILL to ensure the process stopped.
     * the stop will wait [0, SRS_PROCESS_QUIT_TIMEOUT_MS] depends on the
     * process quit timeout.
     * @remark use fast_stop before stop one by one, when got lots of process to quit.
     */
    virtual void stop();
public:
    /**
     * the fast stop is to send a SIGTERM.
     * for example, the ingesters owner lots of FFMPEG, it will take a long time
     * to stop one by one, instead the ingesters can fast_stop all FFMPEG, then
     * wait one by one to stop, it's more faster.
     * @remark user must use stop() to ensure the ffmpeg to stopped.
     * @remark we got N processes to stop, compare the time we spend,
     *      when use stop without fast_stop, we spend maybe [0, SRS_PROCESS_QUIT_TIMEOUT_MS * N]
     *      but use fast_stop then stop, the time is almost [0, SRS_PROCESS_QUIT_TIMEOUT_MS].
     */
    virtual void fast_stop();
};

#endif

