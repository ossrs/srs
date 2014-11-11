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

#include <srs_app_poll.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

// the interval in us to refresh the poll for all fds.
// for performance refine, @see: https://github.com/winlinvip/simple-rtmp-server/issues/194
#define SRS_POLL_CYCLE_INTERVAL 10 * 1000 * 1000

SrsPoll::SrsPoll()
{
    _pds = NULL;
    pthread = new SrsThread(this, 0, false);
}

SrsPoll::~SrsPoll()
{
    srs_freep(_pds);
    srs_freep(pthread);
    fds.clear();
}

int SrsPoll::start()
{
    return pthread->start();
}

int SrsPoll::cycle()
{
    int ret = ERROR_SUCCESS;
    
    if (fds.size() == 0) {
        st_usleep(SRS_CONSTS_RTMP_PULSE_TIMEOUT_US);
        return ret;
    }
    
    int nb_pds = (int)fds.size();
    
    // TODO: FIXME: use more efficient way for the poll.
    srs_freep(_pds);
    _pds = new pollfd[nb_pds];
    
    if (true) {
        int index = 0;

        std::map<int, SrsPollFD*>::iterator it;
        for (it = fds.begin(); it != fds.end(); ++it) {
            int fd = it->first;
            
            pollfd& pfd = _pds[index++];
            pfd.fd = fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
        }
        
        srs_assert(index == (int)fds.size());
    }

    // Upon successful completion, a non-negative value is returned. 
    // A positive value indicates the total number of OS file descriptors in pds that have events. 
    // A value of 0 indicates that the call timed out.
    if (st_poll(_pds, nb_pds, SRS_POLL_CYCLE_INTERVAL) < 0) {
        srs_warn("ignore st_poll failed, size=%d", nb_pds);
        return ret;
    }
    
    for (int i = 0; i < nb_pds; i++) {
        if (!(_pds[i].revents & POLLIN)) {
            continue;
        }
        
        int fd = _pds[i].fd;
        if (fds.find(fd) == fds.end()) {
            continue;
        }
        
        SrsPollFD* owner = fds[fd];
        owner->set_active(true);
    }
    
    return ret;
}

int SrsPoll::add(st_netfd_t stfd, SrsPollFD* owner)
{
    int ret = ERROR_SUCCESS;
    
    int fd = st_netfd_fileno(stfd);
    if (fds.find(fd) != fds.end()) {
        ret = ERROR_RTMP_POLL_FD_DUPLICATED;
        srs_error("fd exists, fd=%d, ret=%d", fd, ret);
        return ret;
    }
    
    fds[fd] = owner;
    
    return ret;
}

void SrsPoll::remove(st_netfd_t stfd, SrsPollFD* owner)
{
    std::map<int, SrsPollFD*>::iterator it;
    
    int fd = st_netfd_fileno(stfd);
    if ((it = fds.find(fd)) != fds.end()) {
        fds.erase(it);
    }
}

SrsPoll* SrsPoll::_instance = new SrsPoll();

SrsPoll* SrsPoll::instance()
{
    return _instance;
}

SrsPollFD::SrsPollFD()
{
    _stfd = NULL;
    _active = false;
}

SrsPollFD::~SrsPollFD()
{
    if (_stfd) {
        SrsPoll* poll = SrsPoll::instance();
        poll->remove(_stfd, this);
    }
}

int SrsPollFD::initialize(st_netfd_t stfd)
{
    int ret = ERROR_SUCCESS;
    
    _stfd = stfd;
    
    SrsPoll* poll = SrsPoll::instance();
    if ((ret = poll->add(stfd, this)) != ERROR_SUCCESS) {
        srs_error("add fd to poll failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

bool SrsPollFD::active()
{
    return _active;
}

void SrsPollFD::set_active(bool v)
{
    _active = v;
}

