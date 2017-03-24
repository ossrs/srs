/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#include <srs_rtmp_msg_array.hpp>

#include <srs_rtmp_stack.hpp>

SrsMessageArray::SrsMessageArray(int max_msgs)
{
    srs_assert(max_msgs > 0);
    
    msgs = new SrsSharedPtrMessage*[max_msgs];
    max = max_msgs;
    
    zero(max_msgs);
}

SrsMessageArray::~SrsMessageArray()
{
    // we just free the msgs itself,
    // both delete and delete[] is ok,
    // for each msg in msgs is already freed by send_and_free_messages.
    srs_freepa(msgs);
}

void SrsMessageArray::free(int count)
{
    // initialize
    for (int i = 0; i < count; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        srs_freep(msg);
        
        msgs[i] = NULL;
    }
}

void SrsMessageArray::zero(int count)
{
    // initialize
    for (int i = 0; i < count; i++) {
        msgs[i] = NULL;
    }
}


