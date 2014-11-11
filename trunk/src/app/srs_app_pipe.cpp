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

#include <srs_app_pipe.hpp>

#include <unistd.h>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

SrsPipe::SrsPipe()
{
    fds[0] = fds[1] = 0;
    read_stfd = write_stfd = NULL;
    _already_written = false;
}

SrsPipe::~SrsPipe()
{
    srs_close_stfd(read_stfd);
    srs_close_stfd(write_stfd);
}

int SrsPipe::initialize()
{
    int ret = ERROR_SUCCESS;
    
    if (pipe(fds) < 0) {
        ret = ERROR_SYSTEM_CREATE_PIPE;
        srs_error("create pipe failed. ret=%d", ret);
        return ret;
    }
    
    if ((read_stfd = st_netfd_open(fds[0])) == NULL) {
        ret = ERROR_SYSTEM_CREATE_PIPE;
        srs_error("open read pipe failed. ret=%d", ret);
        return ret;
    }
    
    if ((write_stfd = st_netfd_open(fds[1])) == NULL) {
        ret = ERROR_SYSTEM_CREATE_PIPE;
        srs_error("open write pipe failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

st_netfd_t SrsPipe::rfd()
{
    return read_stfd;
}

bool SrsPipe::already_written()
{
    return _already_written;
}

int SrsPipe::active()
{
    int ret = ERROR_SUCCESS;
    
    int v = 0;
    if (st_write(read_stfd, &v, sizeof(int), ST_UTIME_NO_TIMEOUT) != sizeof(int)) {
        ret = ERROR_SYSTEM_WRITE_PIPE;
        srs_error("write pipe failed. ret=%d", ret);
        return ret;
    }
    
    _already_written = true;
    
    return ret;
}

int SrsPipe::reset()
{
    int ret = ERROR_SUCCESS;
    
    int v;
    if (st_read(read_stfd, &v, sizeof(int), ST_UTIME_NO_TIMEOUT) != sizeof(int)) {
        ret = ERROR_SYSTEM_READ_PIPE;
        srs_error("read pipe failed. ret=%d", ret);
        return ret;
    }
    
    _already_written = false;
    
    return ret;
}

