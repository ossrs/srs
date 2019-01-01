/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

#include <srs_app_hourglass.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_log.hpp>

ISrsHourGlass::ISrsHourGlass()
{
}

ISrsHourGlass::~ISrsHourGlass()
{
}

SrsHourGlass::SrsHourGlass(ISrsHourGlass* h, int resolution_ms)
{
    handler = h;
    resolution = resolution_ms;
    total_elapse = 0;
}

SrsHourGlass::~SrsHourGlass()
{
}

srs_error_t SrsHourGlass::tick(int type, int interval)
{
    srs_error_t err = srs_success;
    
    if (resolution > 0 && (interval % resolution) != 0) {
        return srs_error_new(ERROR_SYSTEM_HOURGLASS_RESOLUTION, "hourglass interval=%d invalid, resolution=%d", interval, resolution);
    }
    
    ticks[type] = interval;
    
    return err;
}

srs_error_t SrsHourGlass::cycle()
{
    srs_error_t err = srs_success;
    
    map<int, int>::iterator it;
    for (it = ticks.begin(); it != ticks.end(); ++it) {
        int type = it->first;
        int interval = it->second;
        
        if (interval == 0 || (total_elapse % interval) == 0) {
            if ((err = handler->notify(type, interval, total_elapse)) != srs_success) {
                return srs_error_wrap(err, "notify");
            }
        }
    }
    
    total_elapse += resolution;
    srs_usleep(resolution * 1000);
    
    return err;
}
