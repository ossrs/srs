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

#ifndef SRS_APP_PIPE_HPP
#define SRS_APP_PIPE_HPP

/*
#include <srs_app_pipe.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_st.hpp>

/**
* convert something to io, 
* for example, signal or SrsConsumer event.
* for performance issue, @see: https://github.com/winlinvip/simple-rtmp-server/issues/194
*/
class SrsPipe
{
private:
    int fds[2];
    st_netfd_t read_stfd;
    st_netfd_t write_stfd;
    /**
    * for the event based service, 
    * for example, the consumer only care whether there is data writen in pipe,
    * and the source will not write to pipe when pipe is already writen.
    */
    bool _already_written;
public:
    SrsPipe();
    virtual ~SrsPipe();
public:
    /**
    * initialize pipes, open fds.
    */
    virtual int initialize();
public:
    /**
    * for event based service, whether already writen data.
    */
    virtual bool already_written();
    /**
    * for event based service, 
    * write an int to pipe and set the pipe to active.
    */
    virtual int active();
    /**
    * for event based service,
    * read an int from pipe and reset the pipe to deactive.
    */
    virtual int reset();
};

#endif

