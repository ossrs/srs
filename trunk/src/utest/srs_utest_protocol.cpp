/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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
#include <srs_utest_protocol.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>

VOID TEST(ProtocolUtilityTest, VhostResolve)
{
    std::string vhost = "vhost";
    std::string app = "app";
    srs_vhost_resolve(vhost, app);
    EXPECT_STREQ("vhost", vhost.c_str());
    EXPECT_STREQ("app", app.c_str());
    
    app = "app?vhost=changed";
    srs_vhost_resolve(vhost, app);
    EXPECT_STREQ("changed", vhost.c_str());
    EXPECT_STREQ("app", app.c_str());
    
    app = "app?vhost=changed1&&query=true";
    srs_vhost_resolve(vhost, app);
    EXPECT_STREQ("changed1", vhost.c_str());
    EXPECT_STREQ("app", app.c_str());
    
    app = "app?other=true&&vhost=changed2&&query=true";
    srs_vhost_resolve(vhost, app);
    EXPECT_STREQ("changed2", vhost.c_str());
    EXPECT_STREQ("app", app.c_str());
    
    app = "app...other...true...vhost...changed3...query...true";
    srs_vhost_resolve(vhost, app);
    EXPECT_STREQ("changed3", vhost.c_str());
    EXPECT_STREQ("app", app.c_str());
}

VOID TEST(ProtocolUtilityTest, DiscoveryTcUrl)
{
    std::string tcUrl; 
    std::string schema; std::string host; std::string vhost; 
    std::string app; std::string port;
    
    tcUrl = "rtmp://127.0.0.1:1935/live";
    srs_discovery_tc_url(tcUrl, schema, host, vhost, app, port);
    EXPECT_STREQ("rtmp", schema.c_str());
    EXPECT_STREQ("127.0.0.1", host.c_str());
    EXPECT_STREQ("127.0.0.1", vhost.c_str());
    EXPECT_STREQ("live", app.c_str());
    EXPECT_STREQ("1935", port.c_str());
    
    tcUrl = "rtmp://127.0.0.1:19351/live";
    srs_discovery_tc_url(tcUrl, schema, host, vhost, app, port);
    EXPECT_STREQ("rtmp", schema.c_str());
    EXPECT_STREQ("127.0.0.1", host.c_str());
    EXPECT_STREQ("127.0.0.1", vhost.c_str());
    EXPECT_STREQ("live", app.c_str());
    EXPECT_STREQ("19351", port.c_str());
    
    tcUrl = "rtmp://127.0.0.1:19351/live?vhost=demo";
    srs_discovery_tc_url(tcUrl, schema, host, vhost, app, port);
    EXPECT_STREQ("rtmp", schema.c_str());
    EXPECT_STREQ("127.0.0.1", host.c_str());
    EXPECT_STREQ("demo", vhost.c_str());
    EXPECT_STREQ("live", app.c_str());
    EXPECT_STREQ("19351", port.c_str());
    
    tcUrl = "rtmp://127.0.0.1:19351/live/show?vhost=demo";
    srs_discovery_tc_url(tcUrl, schema, host, vhost, app, port);
    EXPECT_STREQ("rtmp", schema.c_str());
    EXPECT_STREQ("127.0.0.1", host.c_str());
    EXPECT_STREQ("demo", vhost.c_str());
    EXPECT_STREQ("live/show", app.c_str());
    EXPECT_STREQ("19351", port.c_str());
}

VOID TEST(ProtocolUtilityTest, GenerateTcUrl)
{
    string ip; string vhost; string app; string port; string tcUrl;
    
    ip = "127.0.0.1"; vhost = "__defaultVhost__"; app = "live"; port = "1935";
    tcUrl = srs_generate_tc_url(ip, vhost, app, port);
    EXPECT_STREQ("rtmp://127.0.0.1/live", tcUrl.c_str());
    
    ip = "127.0.0.1"; vhost = "demo"; app = "live"; port = "1935";
    tcUrl = srs_generate_tc_url(ip, vhost, app, port);
    EXPECT_STREQ("rtmp://demo/live", tcUrl.c_str());
    
    ip = "127.0.0.1"; vhost = "demo"; app = "live"; port = "19351";
    tcUrl = srs_generate_tc_url(ip, vhost, app, port);
    EXPECT_STREQ("rtmp://demo:19351/live", tcUrl.c_str());
}
