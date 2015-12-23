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

#include <srs_app_refer.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>

SrsRefer::SrsRefer()
{
}

SrsRefer::~SrsRefer()
{
}

int SrsRefer::check(std::string page_url, SrsConfDirective* refer)
{
    int ret = ERROR_SUCCESS;
    
    if (!refer) {
        srs_verbose("ignore refer check for page_url=%s", page_url.c_str());
        return ret;
    }
    
    for (int i = 0; i < (int)refer->args.size(); i++) {
        if ((ret = check_single_refer(page_url, refer->args.at(i))) == ERROR_SUCCESS) {
            srs_verbose("check refer success. page_url=%s, refer=%s",
                page_url.c_str(), refer->args.at(i).c_str());
            return ret;
        }
    }
    
    ret = ERROR_RTMP_ACCESS_DENIED;
    srs_error("check refer failed. ret=%d", ret);
    
    return ret;
}

int SrsRefer::check_single_refer(std::string page_url, std::string refer)
{
    int ret = ERROR_SUCCESS;
    
    size_t pos = std::string::npos;
    
    std::string domain_name = page_url;
    if ((pos = domain_name.find("://")) != std::string::npos) {
        domain_name = domain_name.substr(pos + 3);
    }
    
    if ((pos = domain_name.find("/")) != std::string::npos) {
        domain_name = domain_name.substr(0, pos);
    }
    
    if ((pos = domain_name.find(":")) != std::string::npos) {
        domain_name = domain_name.substr(0, pos);
    }
    
    pos = domain_name.find(refer);
    if (pos == std::string::npos) {
        ret = ERROR_RTMP_ACCESS_DENIED;
    }
    // match primary domain.
    if (pos != domain_name.length() - refer.length()) {
        ret = ERROR_RTMP_ACCESS_DENIED;
    }
    
    if (ret != ERROR_SUCCESS) {
        srs_verbose("access denied, page_url=%s, domain_name=%s, refer=%s, ret=%d",
            page_url.c_str(), domain_name.c_str(), refer.c_str(), ret);
    }
    
    return ret;
}


