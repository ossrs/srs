/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

#include <srs_app_st.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

#ifdef __linux__
#include <sys/epoll.h>

bool srs_st_epoll_is_supported(void)
{
    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    /* Guaranteed to fail */
    epoll_ctl(-1, EPOLL_CTL_ADD, -1, &ev);

    return (errno != ENOSYS);
}
#endif

int srs_init_st()
{
    int ret = ERROR_SUCCESS;
    
#ifdef __linux__
    // check epoll, some old linux donot support epoll.
    // @see https://github.com/simple-rtmp-server/srs/issues/162
    if (!srs_st_epoll_is_supported()) {
        ret = ERROR_ST_SET_EPOLL;
        srs_error("epoll required on Linux. ret=%d", ret);
        return ret;
    }
#endif
    
    // Select the best event system available on the OS. In Linux this is
    // epoll(). On BSD it will be kqueue.
    if (st_set_eventsys(ST_EVENTSYS_ALT) == -1) {
        ret = ERROR_ST_SET_EPOLL;
        srs_error("st_set_eventsys use %s failed. ret=%d", st_get_eventsys_name(), ret);
        return ret;
    }
    srs_trace("st_set_eventsys to %s", st_get_eventsys_name());

    if(st_init() != 0){
        ret = ERROR_ST_INITIALIZE;
        srs_error("st_init failed. ret=%d", ret);
        return ret;
    }
    srs_trace("st_init success, use %s", st_get_eventsys_name());
    
    return ret;
}

void srs_close_stfd(st_netfd_t& stfd)
{
    if (stfd) {
        int fd = st_netfd_fileno(stfd);
        st_netfd_close(stfd);
        stfd = NULL;
        
        // st does not close it sometimes, 
        // close it manually.
        close(fd);
    }
}

