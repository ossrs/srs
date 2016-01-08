/*
 The MIT License (MIT)
 
 Copyright (c) 2013-2016 SRS(ossrs)
 
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

int SrsHourGlass::tick(int type, int interval)
{
    int ret = ERROR_SUCCESS;
    
    if (resolution > 0 && (interval % resolution) != 0) {
        ret = ERROR_SYSTEM_HOURGLASS_RESOLUTION;
        srs_error("hourglass interval=%d invalid, resolution=%d. ret=%d", interval, resolution, ret);
        return ret;
    }
    
    ticks[type] = interval;
    
    return ret;
}

int SrsHourGlass::cycle()
{
    int ret = ERROR_SUCCESS;
    
    map<int, int>::iterator it;
    for (it = ticks.begin(); it != ticks.end(); ++it) {
        int type = it->first;
        int interval = it->second;
        
        if (interval == 0 || (total_elapse % interval) == 0) {
            if ((ret = handler->notify(type, interval, total_elapse)) != ERROR_SUCCESS) {
                return ret;
            }
        }
    }
    
    total_elapse += resolution;
    st_usleep(resolution * 1000);
    
    return ret;
}
