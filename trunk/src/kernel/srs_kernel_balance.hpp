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

#ifndef SRS_KERNEL_AAC_HPP
#define SRS_KERNEL_AAC_HPP

/*
#include <srs_kernel_balance.hpp>
*/
#include <srs_core.hpp>

#include <vector>
#include <string>

/**
 * the round-robin load balance algorithm,
 * used for edge pull, kafka and other multiple server feature.
 */
class SrsLbRoundRobin
{
private:
    // current selected index.
    int index;
    // total scheduled count.
    u_int32_t count;
    // current selected server.
    std::string elem;
public:
    SrsLbRoundRobin();
    virtual ~SrsLbRoundRobin();
public:
    virtual u_int32_t current();
    virtual std::string selected();
    virtual std::string select(const std::vector<std::string>& servers);
};

#endif

