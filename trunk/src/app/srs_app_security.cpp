/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#include <srs_app_security.hpp>

#include <srs_kernel_error.hpp>
#include <srs_app_config.hpp>

using namespace std;

SrsSecurity::SrsSecurity()
{
}

SrsSecurity::~SrsSecurity()
{
}

int SrsSecurity::check(SrsRtmpConnType type, string ip, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    // allow all if security disabled.
    if (!_srs_config->get_security_enabled(req->vhost)) {
        return ret;
    }
    
    // default to deny all when security enabled.
    ret = ERROR_SYSTEM_SECURITY;
    
    // rules to apply
    SrsConfDirective* rules = _srs_config->get_security_rules(req->vhost);
    if (!rules) {
        return ret;
    }
    
    // allow if matches allow strategy.
    if (allow_check(rules, type, ip) == ERROR_SYSTEM_SECURITY_ALLOW) {
        ret = ERROR_SUCCESS;
    }
    
    // deny if matches deny strategy.
    if (deny_check(rules, type, ip) == ERROR_SYSTEM_SECURITY_DENY) {
        ret = ERROR_SYSTEM_SECURITY_DENY;
    }
    
    return ret;
}

int SrsSecurity::allow_check(SrsConfDirective* rules, SrsRtmpConnType type, std::string ip)
{
    int ret = ERROR_SUCCESS;
    
    for (int i = 0; i < (int)rules->directives.size(); i++) {
        SrsConfDirective* rule = rules->at(i);
        
        if (rule->name != "allow") {
            continue;
        }
        
        switch (type) {
            case SrsRtmpConnPlay:
                if (rule->arg0() != "play") {
                    break;
                }
                if (rule->arg1() == "all" || rule->arg1() == ip) {
                    ret = ERROR_SYSTEM_SECURITY_ALLOW;
                    break;
                }
                break;
            case SrsRtmpConnFMLEPublish:
            case SrsRtmpConnFlashPublish:
                if (rule->arg0() != "publish") {
                    break;
                }
                if (rule->arg1() == "all" || rule->arg1() == ip) {
                    ret = ERROR_SYSTEM_SECURITY_ALLOW;
                    break;
                }
                break;
            case SrsRtmpConnUnknown:
            default:
                break;
        }
        
        // when matched, donot search more.
        if (ret == ERROR_SYSTEM_SECURITY_ALLOW) {
            break;
        }
    }
    
    return ret;
}

int SrsSecurity::deny_check(SrsConfDirective* rules, SrsRtmpConnType type, std::string ip)
{
    int ret = ERROR_SUCCESS;
    
    for (int i = 0; i < (int)rules->directives.size(); i++) {
        SrsConfDirective* rule = rules->at(i);
        
        if (rule->name != "deny") {
            continue;
        }
        
        switch (type) {
            case SrsRtmpConnPlay:
                if (rule->arg0() != "play") {
                    break;
                }
                if (rule->arg1() == "all" || rule->arg1() == ip) {
                    ret = ERROR_SYSTEM_SECURITY_DENY;
                    break;
                }
                break;
            case SrsRtmpConnFMLEPublish:
            case SrsRtmpConnFlashPublish:
                if (rule->arg0() != "publish") {
                    break;
                }
                if (rule->arg1() == "all" || rule->arg1() == ip) {
                    ret = ERROR_SYSTEM_SECURITY_DENY;
                    break;
                }
                break;
            case SrsRtmpConnUnknown:
            default:
                break;
        }
        
        // when matched, donot search more.
        if (ret == ERROR_SYSTEM_SECURITY_DENY) {
            break;
        }
    }
    
    return ret;
}

