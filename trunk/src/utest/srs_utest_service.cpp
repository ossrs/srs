/*
The MIT License (MIT)

Copyright (c) 2013-2019 Winlin

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
#include <srs_utest_service.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_app_listener.hpp>
#include <srs_service_st.hpp>

// Disable coroutine test for OSX.
#if !defined(SRS_OSX)

#include <srs_service_st.hpp>

VOID TEST(ServiceTimeTest, TimeUnit)
{
    EXPECT_EQ(1000, SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(1000*1000, SRS_UTIME_SECONDS);
    EXPECT_EQ(60*1000*1000, SRS_UTIME_MINUTES);
    EXPECT_EQ(3600*1000*1000LL, SRS_UTIME_HOURS);
}

#define MOCK_LISTEN_HOST "127.0.0.1"
#define MOCK_LISTEN_PORT 11935

class MockTcpHandler : public ISrsTcpHandler
{
private:
	srs_netfd_t fd;
public:
	MockTcpHandler();
	virtual ~MockTcpHandler();
public:
    virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
};

MockTcpHandler::MockTcpHandler()
{
	fd = NULL;
}

MockTcpHandler::~MockTcpHandler()
{
	srs_close_stfd(fd);
}

srs_error_t MockTcpHandler::on_tcp_client(srs_netfd_t stfd)
{
	fd = stfd;
	return srs_success;
}

VOID TEST(TCPServerTest, PingPong)
{
	srs_error_t err;
	if (true) {
		MockTcpHandler h;
		SrsTcpListener l(&h, MOCK_LISTEN_HOST, MOCK_LISTEN_PORT);

		HELPER_EXPECT_SUCCESS(l.listen());
		EXPECT_TRUE(l.fd() > 0);
	}
}

#endif
