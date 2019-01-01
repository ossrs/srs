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

srs_error_t SrsRefer::check(std::string page_url, SrsConfDirective* refer)
{
    srs_error_t err = srs_success;
    
    if (!refer) {
        return err;
    }
    
    for (int i = 0; i < (int)refer->args.size(); i++) {
        if ((err = check_single_refer(page_url, refer->args.at(i))) == srs_success) {
            return srs_success;
        }
        
        srs_error_reset(err);
    }
    
    return srs_error_new(ERROR_RTMP_ACCESS_DENIED, "access denied");
}

srs_error_t SrsRefer::check_single_refer(std::string page_url, std::string refer)
{
    srs_error_t err = srs_success;
    
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
        return srs_error_new(ERROR_RTMP_ACCESS_DENIED, "access denied");
    }
    // match primary domain.
    if (pos != domain_name.length() - refer.length()) {
        return srs_error_new(ERROR_RTMP_ACCESS_DENIED, "access denied");
    }
    
    return err;
}


