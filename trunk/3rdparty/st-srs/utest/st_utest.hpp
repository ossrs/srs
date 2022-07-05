/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2021 Winlin */

#ifndef ST_UTEST_PUBLIC_HPP
#define ST_UTEST_PUBLIC_HPP

// Before define the private/protected, we must include some system header files.
// Or it may fail with:
//      redeclared with different access struct __xfer_bufptrs
// @see https://stackoverflow.com/questions/47839718/sstream-redeclared-with-public-access-compiler-error
#include <gtest/gtest.h>

#define VOID

#endif

