//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_CORE_HPP
#define SRS_UTEST_CORE_HPP

/*
#include <srs_utest_core.hpp>
*/
#include <srs_utest.hpp>

#include <string>

class MyNormalObject
{
private:
    int id_;
public:
    MyNormalObject(int id) {
        id_ = id;
    }
    int id() {
        return id_;
    }
};

#endif

