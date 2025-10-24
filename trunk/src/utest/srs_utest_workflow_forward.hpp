/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2025 Winlin
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

#ifndef SRS_UTEST_FORWARD_HPP
#define SRS_UTEST_FORWARD_HPP

/*
#include <srs_utest_forward.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_forward.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_utest_manual_mock.hpp>

// Mock ISrsAppFactory for testing SrsForwarder
class MockAppFactoryForForwarder : public SrsAppFactory
{
public:
    MockRtmpClient *mock_rtmp_client_;

public:
    MockAppFactoryForForwarder();
    virtual ~MockAppFactoryForForwarder();
    virtual ISrsBasicRtmpClient *create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto);
};

#endif
