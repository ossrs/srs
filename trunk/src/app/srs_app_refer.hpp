//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_REFER_HPP
#define SRS_APP_REFER_HPP

#include <srs_core.hpp>

#include <string>

class SrsConfDirective;

class SrsRefer
{
public:
    SrsRefer();
    virtual ~SrsRefer();
public:
    // Check the refer.
    // @param page_url the client page url.
    // @param refer the refer in config.
    virtual srs_error_t check(std::string page_url, SrsConfDirective* refer);
private:
    virtual srs_error_t check_single_refer(std::string page_url, std::string refer);
};

#endif
