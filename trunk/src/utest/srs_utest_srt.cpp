//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_srt.hpp>

#include <srs_kernel_error.hpp>
#include <srt_conn.hpp>

#include <vector>
using namespace std;

VOID TEST(ProtocolSrtTest, SrtGetStreamInfoNormal) {
    if (true) {
        int mode; string vhost; string subpath;
        EXPECT_TRUE(get_streamid_info("#!::r=live/livestream,key1=value1,key2=value2", mode, vhost, subpath));
        EXPECT_EQ(PULL_SRT_MODE, mode);
        EXPECT_STREQ("", vhost.c_str());
        EXPECT_STREQ("live/livestream?key1=value1&key2=value2", subpath.c_str());
    }

    if (true) {
        int mode; string vhost; string subpath;
        EXPECT_TRUE(get_streamid_info("#!::h=host.com,r=live/livestream,key1=value1,key2=value2", mode, vhost, subpath));
        EXPECT_EQ(PULL_SRT_MODE, mode);
        EXPECT_STREQ("host.com", vhost.c_str());
        EXPECT_STREQ("live/livestream?vhost=host.com&key1=value1&key2=value2", subpath.c_str());
    }
}

VOID TEST(ProtocolSrtTest, SrtGetStreamInfoMethod) {
    if (true) {
        int mode; string vhost; string subpath;
        EXPECT_TRUE(get_streamid_info("#!::r=live/livestream,m=request", mode, vhost, subpath));
        EXPECT_EQ(PULL_SRT_MODE, mode);
        EXPECT_STREQ("live/livestream", subpath.c_str());
    }

    if (true) {
        int mode; string vhost; string subpath;
        EXPECT_TRUE(get_streamid_info("#!::r=live/livestream,m=publish", mode, vhost, subpath));
        EXPECT_EQ(PUSH_SRT_MODE, mode);
        EXPECT_STREQ("live/livestream", subpath.c_str());
    }
}

VOID TEST(ProtocolSrtTest, SrtGetStreamInfoCompatible) {
    if (true) {
        int mode; string vhost; string subpath;
        EXPECT_TRUE(get_streamid_info("#!::h=live/livestream,m=request", mode, vhost, subpath));
        EXPECT_EQ(PULL_SRT_MODE, mode);
        EXPECT_STREQ("", vhost.c_str());
        EXPECT_STREQ("live/livestream", subpath.c_str());
    }

    if (true) {
        int mode; string vhost; string subpath;
        EXPECT_TRUE(get_streamid_info("#!::h=live/livestream,m=publish", mode, vhost, subpath));
        EXPECT_EQ(PUSH_SRT_MODE, mode);
        EXPECT_STREQ("", vhost.c_str());
        EXPECT_STREQ("live/livestream", subpath.c_str());
    }

    if (true) {
        int mode; string vhost; string subpath;
        EXPECT_TRUE(get_streamid_info("#!::h=srs.srt.com.cn/live/livestream,m=request", mode, vhost, subpath));
        EXPECT_EQ(PULL_SRT_MODE, mode);
        EXPECT_STREQ("srs.srt.com.cn", vhost.c_str());
        EXPECT_STREQ("live/livestream?vhost=srs.srt.com.cn", subpath.c_str());
    }

    if (true) {
        int mode; string vhost; string subpath;
        EXPECT_TRUE(get_streamid_info("#!::h=srs.srt.com.cn/live/livestream,m=publish", mode, vhost, subpath));
        EXPECT_EQ(PUSH_SRT_MODE, mode);
        EXPECT_STREQ("srs.srt.com.cn", vhost.c_str());
        EXPECT_STREQ("live/livestream?vhost=srs.srt.com.cn", subpath.c_str());
    }

    if (true) {
        int mode; string vhost; string subpath;
        EXPECT_TRUE(get_streamid_info("#!::h=live/livestream?secret=d6d2be37,m=publish", mode, vhost, subpath));
        EXPECT_EQ(PUSH_SRT_MODE, mode);
        EXPECT_STREQ("", vhost.c_str());
        EXPECT_STREQ("live/livestream?secret=d6d2be37", subpath.c_str());
    }
}

