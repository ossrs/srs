/*
The MIT License (MIT)

Copyright (c) 2013-2020 Winlin

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
#include <srs_utest_config.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_source.hpp>
#include <srs_core_performance.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_service_st.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_utest_kernel.hpp>

MockSrsConfigBuffer::MockSrsConfigBuffer(string buf)
{
    // read all.
    int filesize = (int)buf.length();
    
    if (filesize <= 0) {
        return;
    }
    
    // create buffer
    pos = last = start = new char[filesize];
    end = start + filesize;
    
    memcpy(start, buf.data(), filesize);
}

MockSrsConfigBuffer::~MockSrsConfigBuffer()
{
}

srs_error_t MockSrsConfigBuffer::fullfill(const char* /*filename*/)
{
    return srs_success;
}

MockSrsConfig::MockSrsConfig()
{
}

MockSrsConfig::~MockSrsConfig()
{
}

srs_error_t MockSrsConfig::parse(string buf)
{
    srs_error_t err = srs_success;

    MockSrsConfigBuffer buffer(buf);

    if ((err = parse_buffer(&buffer)) != srs_success) {
        return srs_error_wrap(err, "parse buffer");
    }
    
    if ((err = srs_config_transform_vhost(root)) != srs_success) {
        return srs_error_wrap(err, "transform config");
    }

    if ((err = check_normal_config()) != srs_success) {
        return srs_error_wrap(err, "check normal config");
    }
    
    return err;
}

VOID TEST(ConfigTest, CheckMacros)
{
#ifndef SRS_CONSTS_LOCALHOST
    EXPECT_TRUE(false);
#endif
}

VOID TEST(ConfigDirectiveTest, ParseEmpty)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(0, (int)conf.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameOnly)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("winlin;");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(0, (int)dir.args.size());
    EXPECT_EQ(0, (int)dir.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg0Only)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("winlin arg0;");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(1, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_EQ(0, (int)dir.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg1Only)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("winlin arg0 arg1;");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(2, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_EQ(0, (int)dir.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2Only)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2;");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(0, (int)dir.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2Dir0)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0;}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(1, (int)dir.directives.size());

    SrsConfDirective& dir0 = *dir.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2Dir0NoEmpty)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2{dir0;}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(1, (int)dir.directives.size());

    SrsConfDirective& dir0 = *dir.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2_Dir0Arg0)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0 dir_arg0;}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(1, (int)dir.directives.size());

    SrsConfDirective& dir0 = *dir.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("dir_arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2_Dir0Arg0_Dir0)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0 dir_arg0 {ddir0;}}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(1, (int)dir.directives.size());

    SrsConfDirective& dir0 = *dir.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("dir_arg0", dir0.arg0().c_str());
    EXPECT_EQ(1, (int)dir0.directives.size());

    SrsConfDirective& ddir0 = *dir0.directives.at(0);
    EXPECT_STREQ("ddir0", ddir0.name.c_str());
    EXPECT_EQ(0, (int)ddir0.args.size());
    EXPECT_EQ(0, (int)ddir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2_Dir0Arg0_Dir0Arg0)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0 dir_arg0 {ddir0 ddir_arg0;}}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(1, (int)dir.directives.size());

    SrsConfDirective& dir0 = *dir.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("dir_arg0", dir0.arg0().c_str());
    EXPECT_EQ(1, (int)dir0.directives.size());

    SrsConfDirective& ddir0 = *dir0.directives.at(0);
    EXPECT_STREQ("ddir0", ddir0.name.c_str());
    EXPECT_EQ(1, (int)ddir0.args.size());
    EXPECT_STREQ("ddir_arg0", ddir0.arg0().c_str());
    EXPECT_EQ(0, (int)ddir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, Parse2SingleDirs)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 arg0;dir1 arg1;");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(2, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());

    SrsConfDirective& dir1 = *conf.directives.at(1);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(1, (int)dir1.args.size());
    EXPECT_STREQ("arg1", dir1.arg0().c_str());
    EXPECT_EQ(0, (int)dir1.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseSingleComplexDirs)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 arg0;dir1 {dir2 arg2;}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(2, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());

    SrsConfDirective& dir1 = *conf.directives.at(1);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(0, (int)dir1.args.size());
    EXPECT_EQ(1, (int)dir1.directives.size());

    SrsConfDirective& dir2 = *dir1.directives.at(0);
    EXPECT_STREQ("dir2", dir2.name.c_str());
    EXPECT_EQ(1, (int)dir2.args.size());
    EXPECT_STREQ("arg2", dir2.arg0().c_str());
    EXPECT_EQ(0, (int)dir2.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseStringArgs)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 arg0 \"str_arg\" 100;");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(3, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_STREQ("str_arg", dir0.arg1().c_str());
    EXPECT_STREQ("100", dir0.arg2().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseStringArgsWithSpace)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 arg0 \"str_arg space\" 100;");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(3, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_STREQ("str_arg space", dir0.arg1().c_str());
    EXPECT_STREQ("100", dir0.arg2().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNumberArgs)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 100 101 102;");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(3, (int)dir0.args.size());
    EXPECT_EQ(100, ::atoi(dir0.arg0().c_str()));
    EXPECT_EQ(101, ::atoi(dir0.arg1().c_str()));
    EXPECT_EQ(102, ::atoi(dir0.arg2().c_str()));
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseFloatArgs)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 100.01 101.02 102.03;");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(3, (int)dir0.args.size());
    EXPECT_FLOAT_EQ(100.01, ::atof(dir0.arg0().c_str()));
    EXPECT_FLOAT_EQ(101.02, ::atof(dir0.arg1().c_str()));
    EXPECT_FLOAT_EQ(102.03, ::atof(dir0.arg2().c_str()));
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseComments)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("#commnets\ndir0 arg0;\n#end-comments");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseCommentsInline)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("#commnets\ndir0 arg0;#inline comments\n#end-comments");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseCommentsInlineWithSpace)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf(" #commnets\ndir0 arg0; #inline comments\n #end-comments");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseCommentsInlinemixed)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("#commnets\ndir0 arg0;#inline comments\n#end-comments\ndir1 arg1;");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(2, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());

    SrsConfDirective& dir1 = *conf.directives.at(1);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(1, (int)dir1.args.size());
    EXPECT_STREQ("arg1", dir1.arg0().c_str());
    EXPECT_EQ(0, (int)dir1.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseSpecialChars)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 http://www.ossrs.net/api/v1/versions?level=major;");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("http://www.ossrs.net/api/v1/versions?level=major", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseSpecialChars2)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 rtmp://[server]:[port]/[app]/[stream]_[engine];");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("rtmp://[server]:[port]/[app]/[stream]_[engine]", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseInvalidNoEndOfDirective)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0");
    SrsConfDirective conf;
    HELPER_ASSERT_FAILED(conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidNoEndOfSubDirective)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 {");
    SrsConfDirective conf;
    HELPER_ASSERT_FAILED(conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidNoStartOfSubDirective)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 }");
    SrsConfDirective conf;
    HELPER_ASSERT_FAILED(conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidEmptyName)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf(";");
    SrsConfDirective conf;
    HELPER_ASSERT_FAILED(conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidEmptyName2)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("{}");
    SrsConfDirective conf;
    HELPER_ASSERT_FAILED(conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidEmptyDirective)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 {}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseLine)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 {}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(0, (int)dir0.directives.size());
    EXPECT_EQ(1, (int)dir0.conf_line);
}

VOID TEST(ConfigDirectiveTest, ParseLine2)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("\n\ndir0 {}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(0, (int)dir0.directives.size());
    EXPECT_EQ(3, (int)dir0.conf_line);
}

VOID TEST(ConfigDirectiveTest, ParseLine3)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 {\n\ndir1 arg0;}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(1, (int)dir0.directives.size());
    EXPECT_EQ(1, (int)dir0.conf_line);

    SrsConfDirective& dir1 = *dir0.directives.at(0);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(1, (int)dir1.args.size());
    EXPECT_STREQ("arg0", dir1.arg0().c_str());
    EXPECT_EQ(0, (int)dir1.directives.size());
    EXPECT_EQ(3, (int)dir1.conf_line);
}

VOID TEST(ConfigDirectiveTest, ParseLine4)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 {\n\ndir1 \n\narg0;dir2 arg1;}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(2, (int)dir0.directives.size());
    EXPECT_EQ(1, (int)dir0.conf_line);

    SrsConfDirective& dir1 = *dir0.directives.at(0);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(1, (int)dir1.args.size());
    EXPECT_STREQ("arg0", dir1.arg0().c_str());
    EXPECT_EQ(0, (int)dir1.directives.size());
    EXPECT_EQ(3, (int)dir1.conf_line);

    SrsConfDirective& dir2 = *dir0.directives.at(1);
    EXPECT_STREQ("dir2", dir2.name.c_str());
    EXPECT_EQ(1, (int)dir2.args.size());
    EXPECT_STREQ("arg1", dir2.arg0().c_str());
    EXPECT_EQ(0, (int)dir2.directives.size());
    EXPECT_EQ(5, (int)dir2.conf_line);
}

VOID TEST(ConfigDirectiveTest, ParseLineNormal)
{
    srs_error_t err;
    
    MockSrsConfigBuffer buf("dir0 {\ndir1 {\ndir2 arg2;\n}\n}");
    SrsConfDirective conf;
    HELPER_ASSERT_SUCCESS(conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());

    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(1, (int)dir0.directives.size());
    EXPECT_EQ(1, (int)dir0.conf_line);

    SrsConfDirective& dir1 = *dir0.directives.at(0);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(0, (int)dir1.args.size());
    EXPECT_EQ(1, (int)dir1.directives.size());
    EXPECT_EQ(2, (int)dir1.conf_line);

    SrsConfDirective& dir2 = *dir1.directives.at(0);
    EXPECT_STREQ("dir2", dir2.name.c_str());
    EXPECT_EQ(1, (int)dir2.args.size());
    EXPECT_STREQ("arg2", dir2.arg0().c_str());
    EXPECT_EQ(0, (int)dir2.directives.size());
    EXPECT_EQ(3, (int)dir2.conf_line);
}

VOID TEST(ConfigMainTest, ParseEmpty)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_FAILED(conf.parse(""));
}

VOID TEST(ConfigMainTest, ParseMinConf)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));

    vector<string> listens = conf.get_listens();
    EXPECT_EQ(1, (int)listens.size());
    EXPECT_STREQ("1935", listens.at(0).c_str());
}

VOID TEST(ConfigMainTest, ParseInvalidDirective)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_FAILED(conf.parse("listens 1935;"));
}

VOID TEST(ConfigMainTest, ParseInvalidDirective2)
{
    srs_error_t err;

    MockSrsConfig conf;
    // check error for user not specified the listen directive.
    HELPER_ASSERT_FAILED(conf.parse("chunk_size 4096;"));
}

VOID TEST(ConfigMainTest, CheckConf_listen)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse("listens 1935;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse("listen 0;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse("listen -1;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse("listen -1935;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_pid)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"pids ./objs/srs.pid;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_chunk_size)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"chunk_size 60000;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"chunk_sizes 60000;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"chunk_size 0;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"chunk_size 1;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"chunk_size 127;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"chunk_size -1;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"chunk_size -4096;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"chunk_size 65537;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_ff_log_dir)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"ff_log_dir ./objs;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"ff_log_dirs ./objs;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_srs_log_level)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"srs_log_level trace;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"srs_log_levels trace;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_srs_log_file)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"srs_log_file ./objs/srs.log;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"srs_log_files ./objs/srs.log;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_daemon)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"daemon on;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"daemons on;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_heartbeat)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"heartbeat{}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"heartbeats{}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"heartbeat{enabled on;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"heartbeat{enableds on;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"heartbeat{interval 9;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"heartbeat{intervals 9;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"heartbeat{url http://127.0.0.1:8085/api/v1/servers;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"heartbeat{urls http://127.0.0.1:8085/api/v1/servers;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"heartbeat{device_id 0;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"heartbeat{device_ids 0;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"heartbeat{summaries on;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"heartbeat{summariess on;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_http_api)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"http_api{}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"http_apis{}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"http_api{enableds on;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"http_api{listens 1985;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_stats)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"stats{}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"statss{}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"stats{network 0;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"stats{networks 0;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"stats{network -100;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"stats{network -1;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"stats{disk sda;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"stats{disks sda;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_http_stream)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"http_stream{}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"http_streams{}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"http_stream{enableds on;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"http_stream{listens 8080;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"http_stream{dirs ./objs/nginx/html;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhosts{}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_edge)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{mode remote;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{modes remote;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{origin 127.0.0.1:1935 localhost:1935;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{origins 127.0.0.1:1935 localhost:1935;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{token_traverse off;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{token_traverses off;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_dvr)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{dvr{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{dvrs{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{dvr{enabled on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{dvr{enableds on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_path ./objs/nginx/html;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_paths ./objs/nginx/html;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_plan session;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_plans session;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_duration 30;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_durations 30;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_wait_keyframe on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_wait_keyframes on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{dvr{time_jitter full;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{dvr{time_jitters full;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_ingest)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{ingest{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{ingests{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{ingest{enabled on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{ingest{enableds on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{ingest{input{}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{ingest{inputs{}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{ingest{ffmpeg ./objs/ffmpeg/bin/ffmpeg;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{ingest{ffmpegs ./objs/ffmpeg/bin/ffmpeg;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{ingest{engine{}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{ingest{engines{}}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_http)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{https{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http{enabled on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{http{enableds on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http{mount /hls;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{http{mounts /hls;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http{dir ./objs/nginx/html/hls;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{http{dirs ./objs/nginx/html/hls;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_hls)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{hls{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{hlss{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{hls{enabled on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{hls{enableds on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{hls{hls_path ./objs/nginx/html;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{hls{hls_paths ./objs/nginx/html;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{hls{hls_fragment 10;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{hls{hls_fragments 10;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{hls{hls_window 60;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{hls{hls_windows 60;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_hooks)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{http_hookss{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{enabled on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{enableds on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_connect http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_connects http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_close http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_closes http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_publish http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_publishs http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_unpublish http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_unpublishs http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_play http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_plays http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_stop http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_stops http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_gop_cache)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{gop_cache off;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{gop_caches off;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{queue_length 10;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{queue_lengths 10;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_debug_srs_upnode)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{debug_srs_upnode off;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{debug_srs_upnodes off;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_refer)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{refer github.com github.io;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{refers github.com github.io;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{refer_publish github.com github.io;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{refer_publishs github.com github.io;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{refer_play github.com github.io;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{refer_plays github.com github.io;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_forward)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{forward 127.0.0.1:1936;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{forwards 127.0.0.1:1936;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_transcode)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcodes{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{enabled on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{enableds on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{ffmpeg ./objs/ffmpeg/bin/ffmpeg;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{ffmpegs ./objs/ffmpeg/bin/ffmpeg;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engines {}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {enabled on;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {enableds on;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfilter {}}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfilters {}}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vcodec libx264;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vcodecs libx264;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vbitrate 300;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vbitrates 300;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfps 20;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfpss 20;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vwidth 768;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vwidths 768;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vheight 320;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vheights 320;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vthreads 2;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vthreadss 2;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vprofile baseline;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vprofiles baseline;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vpreset superfast;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vpresets superfast;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vparams {}}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vparamss {}}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {acodec libfdk_aac;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {acodecs libfdk_aac;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {abitrate 45;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {abitrates 45;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {asample_rate 44100;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {asample_rates 44100;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {achannels 2;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {achannelss 2;}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {aparams {}}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {aparamss {}}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {output rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];}}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {outputs rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];}}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_bandcheck)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{bandcheck{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{bandchecks{}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{bandcheck{enabled on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{bandcheck{enableds on;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{bandcheck{key \"35c9b402c12a7246868752e2878f7e0e\";}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{bandcheck{keys \"35c9b402c12a7246868752e2878f7e0e\";}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{bandcheck{interval 30;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{bandcheck{intervals 30;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{bandcheck{limit_kbps 4000;}}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{bandcheck{limit_kbpss 4000;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_chunk_size2)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{chunk_size 128;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{chunk_sizes 128;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{chunk_size 127;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{chunk_size 0;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{chunk_size -1;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{chunk_size -128;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{chunk_size 65537;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_jitter)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{time_jitter full;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{time_jitters full;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_atc)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{atc on;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{atcs on;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{atc_auto on;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{atc_autos on;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{enabled on;}"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{enableds on;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_pithy_print)
{
    srs_error_t err;
    
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"pithy_print_ms 1000;"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"pithy_print_mss 1000;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_ingest_id)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{ingest id{}}"));
    HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{ingest id{} ingest id{}}"));
    HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF"vhost v{ingest{} ingest{}}"));
}

VOID TEST(ConfigUnitTest, CheckDefaultValuesVhost)
{
    srs_error_t err;

    MockSrsConfig conf;

    if (true) {
	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_bw_check_interval(""));

	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{bandcheck{interval 4;}}"));
	    EXPECT_EQ(4 * SRS_UTIME_SECONDS, conf.get_bw_check_interval("v"));
    }

    if (true) {
	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_dash_fragment(""));
	    EXPECT_EQ(150 * SRS_UTIME_SECONDS, conf.get_dash_update_period(""));
	    EXPECT_EQ(300 * SRS_UTIME_SECONDS, conf.get_dash_timeshift(""));

	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{dash{dash_fragment 4;dash_update_period 40;dash_timeshift 70;}}"));
	    EXPECT_EQ(4 * SRS_UTIME_SECONDS, conf.get_dash_fragment("v"));
	    EXPECT_EQ(40 * SRS_UTIME_SECONDS, conf.get_dash_update_period("v"));
	    EXPECT_EQ(70 * SRS_UTIME_SECONDS, conf.get_dash_timeshift("v"));
    }

    if (true) {
	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(srs_utime_t(9.9 * SRS_UTIME_SECONDS), conf.get_heartbeat_interval());

	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"heartbeat{interval 10;}"));
	    EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_heartbeat_interval());
    }

    if (true) {
	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_pithy_print());

	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"pithy_print_ms 20000;"));
	    EXPECT_EQ(20 * SRS_UTIME_SECONDS, conf.get_pithy_print());
    }

    if (true) {
	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(350 * SRS_UTIME_MILLISECONDS, conf.get_mr_sleep(""));
	    EXPECT_EQ(350 * SRS_UTIME_MILLISECONDS, conf.get_mw_sleep(""));
	    EXPECT_EQ(20 * SRS_UTIME_SECONDS, conf.get_publish_1stpkt_timeout(""));
	    EXPECT_EQ(5 * SRS_UTIME_SECONDS, conf.get_publish_normal_timeout(""));

	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{publish{mr_latency 1000; firstpkt_timeout 100; normal_timeout 100;} play{mw_latency 1000;}}"));
	    EXPECT_EQ(1000 * SRS_UTIME_MILLISECONDS, conf.get_mr_sleep("v"));
	    EXPECT_EQ(100 * SRS_UTIME_MILLISECONDS, conf.get_publish_1stpkt_timeout("v"));
	    EXPECT_EQ(100 * SRS_UTIME_MILLISECONDS, conf.get_publish_normal_timeout("v"));
    }

    if (true) {
	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_dvr_duration(""));

	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_duration 10;}}"));
	    EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_dvr_duration("v"));
    }

    if (true) {
	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(0, conf.get_hls_dispose(""));
	    EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hls_fragment(""));
	    EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hls_window(""));

	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{hls{hls_dispose 10;hls_fragment 20;hls_window 30;}}"));
	    EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hls_dispose("v"));
	    EXPECT_EQ(20 * SRS_UTIME_SECONDS, conf.get_hls_fragment("v"));
	    EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_hls_window("v"));
    }

    if (true) {
	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hds_fragment(""));
	    EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hds_window(""));

	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{hds{hds_fragment 20;hds_window 30;}}"));
	    EXPECT_EQ(20 * SRS_UTIME_SECONDS, conf.get_hds_fragment("v"));
	    EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_hds_window("v"));
    }

    if (true) {
	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_queue_length(""));
	    EXPECT_EQ(0, conf.get_send_min_interval(""));

	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{play{queue_length 100;send_min_interval 10;}}"));
	    EXPECT_EQ(100 * SRS_UTIME_SECONDS, conf.get_queue_length("v"));
	    EXPECT_EQ(10 * SRS_UTIME_MILLISECONDS, conf.get_send_min_interval("v"));
    }

    if (true) {
	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(0, conf.get_vhost_http_remux_fast_cache(""));

	    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost v{http_remux{fast_cache 10;}}"));
	    EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_vhost_http_remux_fast_cache("v"));
    }
}

VOID TEST(ConfigUnitTest, CheckDefaultValuesGlobal)
{
    if (true) {
        srs_utime_t t0 = srs_update_system_time();
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        srs_utime_t t1 = srs_update_system_time();

        EXPECT_TRUE(t1 - t0 >= 10 * SRS_UTIME_MILLISECONDS);
    }

    if (true) {
        srs_utime_t t0 = srs_get_system_time();
        srs_utime_t t1 = srs_update_system_time();

        EXPECT_TRUE(t0 > 0);
        EXPECT_TRUE(t1 >= t0);
    }
}

VOID TEST(ConfigUnitTest, VectorEquals)
{
    if (true) {
        vector<uint8_t> a, b;
        a.push_back(0); a.push_back(1); a.push_back(2);
        b.push_back(0); b.push_back(1); b.push_back(2);
        EXPECT_TRUE(srs_vector_actual_equals(a, b));
    }

    if (true) {
        vector<uint8_t> a, b;
        a.push_back(0); a.push_back(1); a.push_back(2);
        b.push_back(2); b.push_back(1); b.push_back(0);
        EXPECT_TRUE(srs_vector_actual_equals(a, b));
    }

    if (true) {
        vector<uint8_t> a, b;
        a.push_back(0); a.push_back(1); a.push_back(2);
        b.push_back(0); b.push_back(2); b.push_back(1);
        EXPECT_TRUE(srs_vector_actual_equals(a, b));
    }

    if (true) {
        vector<uint8_t> a, b;
        a.push_back(0); a.push_back(1); a.push_back(2);
        b.push_back(0); b.push_back(1); b.push_back(2); b.push_back(3);
        EXPECT_FALSE(srs_vector_actual_equals(a, b));
    }

    if (true) {
        vector<uint8_t> a, b;
        a.push_back(1); a.push_back(2); a.push_back(3);
        b.push_back(0); b.push_back(1); b.push_back(2);
        EXPECT_FALSE(srs_vector_actual_equals(a, b));
    }
}

extern bool srs_directive_equals_self(SrsConfDirective* a, SrsConfDirective* b);
extern bool srs_directive_equals(SrsConfDirective* a, SrsConfDirective* b);
extern bool srs_directive_equals(SrsConfDirective* a, SrsConfDirective* b, string except);

VOID TEST(ConfigUnitTest, DirectiveEquals)
{
    EXPECT_TRUE(srs_directive_equals_self(NULL, NULL));

    if (true) {
        SrsConfDirective* a = new SrsConfDirective();
        EXPECT_FALSE(srs_directive_equals_self(a, NULL));
        EXPECT_FALSE(srs_directive_equals_self(NULL, a));
        srs_freep(a);
    }

    if (true) {
        SrsConfDirective* a = new SrsConfDirective();
        SrsConfDirective* b = a;
        EXPECT_TRUE(srs_directive_equals_self(a, b));
        srs_freep(a);
    }

    if (true) {
        SrsConfDirective* a = new SrsConfDirective();
        a->name = "hls";
        SrsConfDirective* b = new SrsConfDirective();
        b->name = "dvr";
        EXPECT_FALSE(srs_directive_equals_self(a, b));
        srs_freep(a); srs_freep(b);
    }

    if (true) {
        SrsConfDirective* a = new SrsConfDirective();
        a->directives.push_back(new SrsConfDirective());
        SrsConfDirective* b = new SrsConfDirective();
        EXPECT_FALSE(srs_directive_equals_self(a, b));
        srs_freep(a); srs_freep(b);
    }

    if (true) {
        SrsConfDirective* a = new SrsConfDirective();
        a->directives.push_back(new SrsConfDirective());
        a->at(0)->name = "hls";
        SrsConfDirective* b = new SrsConfDirective();
        b->directives.push_back(new SrsConfDirective());
        EXPECT_TRUE(srs_directive_equals(a, b, "hls"));
        srs_freep(a); srs_freep(b);
    }
}

VOID TEST(ConfigUnitTest, OperatorEquals)
{
    EXPECT_TRUE(srs_config_hls_is_on_error_ignore("ignore"));
    EXPECT_FALSE(srs_config_hls_is_on_error_ignore("xxx"));

    EXPECT_TRUE(srs_config_hls_is_on_error_continue("continue"));
    EXPECT_FALSE(srs_config_hls_is_on_error_continue("xxx"));

    EXPECT_TRUE(srs_config_ingest_is_file("file"));
    EXPECT_FALSE(srs_config_ingest_is_file("xxx"));

    EXPECT_TRUE(srs_config_ingest_is_stream("stream"));
    EXPECT_FALSE(srs_config_ingest_is_stream("xxx"));

    EXPECT_TRUE(srs_config_dvr_is_plan_segment("segment"));
    EXPECT_FALSE(srs_config_dvr_is_plan_segment("xxx"));

    EXPECT_TRUE(srs_config_dvr_is_plan_session("session"));
    EXPECT_FALSE(srs_config_dvr_is_plan_session("xxx"));

    EXPECT_TRUE(srs_stream_caster_is_udp("mpegts_over_udp"));
    EXPECT_FALSE(srs_stream_caster_is_udp("xxx"));

    EXPECT_TRUE(srs_stream_caster_is_rtsp("rtsp"));
    EXPECT_FALSE(srs_stream_caster_is_rtsp("xxx"));

    EXPECT_TRUE(srs_stream_caster_is_flv("flv"));
    EXPECT_FALSE(srs_stream_caster_is_flv("xxx"));

    EXPECT_STREQ("on", srs_config_bool2switch("true").c_str());
    EXPECT_STREQ("off", srs_config_bool2switch("false").c_str());
    EXPECT_STREQ("off", srs_config_bool2switch("xxx").c_str());
}

VOID TEST(ConfigUnitTest, ApplyFilter)
{
    EXPECT_TRUE(srs_config_apply_filter(NULL, NULL));

    if (true) {
        SrsConfDirective d;
        EXPECT_TRUE(srs_config_apply_filter(&d, NULL));
    }

    if (true) {
        SrsConfDirective d;
        d.args.push_back("all");
        EXPECT_TRUE(srs_config_apply_filter(&d, NULL));
    }

    if (true) {
        SrsConfDirective d;
        SrsRequest r;
        r.app = "live"; r.stream = "stream";
        d.args.push_back("live/stream");
        EXPECT_TRUE(srs_config_apply_filter(&d, &r));
    }

    if (true) {
        SrsConfDirective d;
        d.args.push_back("live/stream");
        SrsRequest r;
        EXPECT_FALSE(srs_config_apply_filter(&d, &r));
    }
}

VOID TEST(ConfigUnitTest, TransformForVhost)
{
    srs_error_t err;

    if (true) {
        SrsConfDirective root;
        root.get_or_create("http_stream");

        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(&root));

        SrsConfDirective* p = root.get("http_server");
        ASSERT_TRUE(p != NULL);
    }

    if (true) {
        SrsConfDirective root;
        SrsConfDirective* vhost = root.get_or_create("vhost");
        if (true) {
            vhost->get_or_create("http");
        }

        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(&root));

        SrsConfDirective* p = vhost->get("http_static");
        ASSERT_TRUE(p != NULL);
    }

    if (true) {
        SrsConfDirective root;
        SrsConfDirective* vhost = root.get_or_create("vhost");
        if (true) {
            SrsConfDirective* p = vhost->get_or_create("http_remux");
            p->get_or_create("hstrs", "on");
        }

        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(&root));

        SrsConfDirective* p = vhost->get("http_remux");
        ASSERT_TRUE(p != NULL);
        ASSERT_TRUE(p->get("hstrs") == NULL);
    }

    if (true) {
        SrsConfDirective root;
        SrsConfDirective* vhost = root.get_or_create("vhost");
        if (true) {
            vhost->get_or_create("refer", "refer-v");
            vhost->get_or_create("refer_play", "refer-play-v");
            vhost->get_or_create("refer_publish", "refer-publish-v");
        }

        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(&root));

        SrsConfDirective* p = vhost->get("refer");
        ASSERT_TRUE(p != NULL);

        ASSERT_TRUE(p->get("enabled") != NULL);
        EXPECT_STREQ("on", p->get("enabled")->arg0().c_str());

        ASSERT_TRUE(p->get("all") != NULL);
        EXPECT_STREQ("refer-v", p->get("all")->arg0().c_str());

        ASSERT_TRUE(p->get("play") != NULL);
        EXPECT_STREQ("refer-play-v", p->get("play")->arg0().c_str());

        ASSERT_TRUE(p->get("publish") != NULL);
        EXPECT_STREQ("refer-publish-v", p->get("publish")->arg0().c_str());
    }

    if (true) {
        SrsConfDirective root;
        SrsConfDirective* vhost = root.get_or_create("vhost");
        if (true) {
            SrsConfDirective* mr = vhost->get_or_create("mr");
            mr->get_or_create("enabled", "on");
            mr->get_or_create("latency", "100");
        }

        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(&root));

        SrsConfDirective* publish = vhost->get("publish");
        ASSERT_TRUE(publish != NULL);

        SrsConfDirective* p = publish->get("mr");
        ASSERT_TRUE(p != NULL);
        EXPECT_STREQ("on", p->arg0().c_str());

        p = publish->get("mr_latency");
        ASSERT_TRUE(p != NULL);
        EXPECT_STREQ("100", p->arg0().c_str());
    }

    if (true) {
        SrsConfDirective root;
        SrsConfDirective* vhost = root.get_or_create("vhost");
        if (true) {
            vhost->get_or_create("publish_1stpkt_timeout", "100");
        }

        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(&root));

        SrsConfDirective* publish = vhost->get("publish");
        ASSERT_TRUE(publish != NULL);

        SrsConfDirective* p = publish->get("firstpkt_timeout");
        ASSERT_TRUE(p != NULL);
        EXPECT_STREQ("100", p->arg0().c_str());
    }

    if (true) {
        SrsConfDirective root;
        SrsConfDirective* vhost = root.get_or_create("vhost");
        if (true) {
            vhost->get_or_create("publish_normal_timeout", "100");
        }

        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(&root));

        SrsConfDirective* publish = vhost->get("publish");
        ASSERT_TRUE(publish != NULL);

        SrsConfDirective* p = publish->get("normal_timeout");
        ASSERT_TRUE(p != NULL);
        EXPECT_STREQ("100", p->arg0().c_str());
    }

    if (true) {
        SrsConfDirective root;
        SrsConfDirective* vhost = root.get_or_create("vhost");
        if (true) {
            vhost->get_or_create("time_jitter", "on");
            vhost->get_or_create("mix_correct", "on");
            vhost->get_or_create("atc", "on");
            vhost->get_or_create("atc_auto", "on");
            vhost->get_or_create("mw_latency", "on");
            vhost->get_or_create("gop_cache", "on");
            vhost->get_or_create("queue_length", "on");
            vhost->get_or_create("send_min_interval", "on");
            vhost->get_or_create("reduce_sequence_header", "on");
        }

        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(&root));

        SrsConfDirective* p = vhost->get("play");
        ASSERT_TRUE(p != NULL);

        ASSERT_TRUE(p->get("time_jitter") != NULL);
        ASSERT_TRUE(p->get("mix_correct") != NULL);
        ASSERT_TRUE(p->get("atc") != NULL);
        ASSERT_TRUE(p->get("atc_auto") != NULL);
        ASSERT_TRUE(p->get("mw_latency") != NULL);
        ASSERT_TRUE(p->get("gop_cache") != NULL);
        ASSERT_TRUE(p->get("queue_length") != NULL);
        ASSERT_TRUE(p->get("send_min_interval") != NULL);
        ASSERT_TRUE(p->get("reduce_sequence_header") != NULL);
    }

    if (true) {
        SrsConfDirective root;
        SrsConfDirective* vhost = root.get_or_create("vhost");
        if (true) {
            vhost->get_or_create("forward", "forward-v");
        }

        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(&root));

        SrsConfDirective* p = vhost->get("forward");
        ASSERT_TRUE(p != NULL);

        ASSERT_TRUE(p->get("enabled") != NULL);
        EXPECT_STREQ("on", p->get("enabled")->arg0().c_str());

        ASSERT_TRUE(p->get("destination") != NULL);
        EXPECT_STREQ("forward-v", p->get("destination")->arg0().c_str());
    }

    if (true) {
        SrsConfDirective root;
        SrsConfDirective* vhost = root.get_or_create("vhost");
        if (true) {
            vhost->get_or_create("mode", "on");
            vhost->get_or_create("origin", "on");
            vhost->get_or_create("token_traverse", "on");
            vhost->get_or_create("vhost", "on");
            vhost->get_or_create("debug_srs_upnode", "on");
        }

        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(&root));

        SrsConfDirective* p = vhost->get("cluster");
        ASSERT_TRUE(p != NULL);

        ASSERT_TRUE(p->get("mode") != NULL);
        ASSERT_TRUE(p->get("origin") != NULL);
        ASSERT_TRUE(p->get("token_traverse") != NULL);
        ASSERT_TRUE(p->get("vhost") != NULL);
        ASSERT_TRUE(p->get("debug_srs_upnode") != NULL);
    }
}

VOID TEST(ConfigUnitTest, DirectiveCopy)
{
    if (true) {
        SrsConfDirective d;
        d.name = "vhost";
        d.get_or_create("enabled", "on");

        SrsConfDirective* cp = d.copy();
        ASSERT_TRUE(cp != NULL);
        EXPECT_STREQ("vhost", cp->name.c_str());
        ASSERT_TRUE(cp->get("enabled") != NULL);
        EXPECT_STREQ("on", cp->get("enabled")->arg0().c_str());
        srs_freep(cp);
    }

    if (true) {
        SrsConfDirective d;
        d.name = "vhost";
        d.get_or_create("enabled", "on");

        SrsConfDirective* cp = d.copy("enabled");
        ASSERT_TRUE(cp != NULL);
        EXPECT_STREQ("vhost", cp->name.c_str());
        ASSERT_TRUE(cp->get("enabled") == NULL);
        srs_freep(cp);
    }

    if (true) {
        SrsConfDirective d;
        d.name = "vhost";
        d.get_or_create("enabled", "on");
        d.get_or_create("hls");

        SrsConfDirective* cp = d.copy("hls");
        ASSERT_TRUE(cp != NULL);
        EXPECT_STREQ("vhost", cp->name.c_str());
        ASSERT_TRUE(cp->get("enabled") != NULL);
        EXPECT_STREQ("on", cp->get("enabled")->arg0().c_str());
        srs_freep(cp);
    }

    if (true) {
        SrsConfDirective d;
        EXPECT_TRUE(d.arg0().empty());
        EXPECT_TRUE(d.arg1().empty());
        EXPECT_TRUE(d.arg2().empty());
        EXPECT_TRUE(d.arg3().empty());
    }

    if (true) {
        SrsConfDirective d;
        d.args.push_back("a0");
        EXPECT_STREQ("a0", d.arg0().c_str());
        EXPECT_TRUE(d.arg1().empty());
        EXPECT_TRUE(d.arg2().empty());
        EXPECT_TRUE(d.arg3().empty());
    }

    if (true) {
        SrsConfDirective d;
        d.args.push_back("a0");
        d.args.push_back("a1");
        EXPECT_STREQ("a0", d.arg0().c_str());
        EXPECT_STREQ("a1", d.arg1().c_str());
        EXPECT_TRUE(d.arg2().empty());
        EXPECT_TRUE(d.arg3().empty());
    }

    if (true) {
        SrsConfDirective d;
        d.args.push_back("a0");
        d.args.push_back("a1");
        d.args.push_back("a2");
        EXPECT_STREQ("a0", d.arg0().c_str());
        EXPECT_STREQ("a1", d.arg1().c_str());
        EXPECT_STREQ("a2", d.arg2().c_str());
        EXPECT_TRUE(d.arg3().empty());
    }

    if (true) {
        SrsConfDirective d;
        d.args.push_back("a0");
        d.args.push_back("a1");
        d.args.push_back("a2");
        d.args.push_back("a3");
        EXPECT_STREQ("a0", d.arg0().c_str());
        EXPECT_STREQ("a1", d.arg1().c_str());
        EXPECT_STREQ("a2", d.arg2().c_str());
        EXPECT_STREQ("a3", d.arg3().c_str());
    }

    if (true) {
        SrsConfDirective d;
        d.set_arg0("a0");
        EXPECT_STREQ("a0", d.arg0().c_str());
        EXPECT_TRUE(d.arg1().empty());
        EXPECT_TRUE(d.arg2().empty());
        EXPECT_TRUE(d.arg3().empty());
    }

    if (true) {
        SrsConfDirective d;
        d.args.push_back("a0");
        d.set_arg0("a0");
        EXPECT_STREQ("a0", d.arg0().c_str());
        EXPECT_TRUE(d.arg1().empty());
        EXPECT_TRUE(d.arg2().empty());
        EXPECT_TRUE(d.arg3().empty());
    }

    if (true) {
        SrsConfDirective d;
        d.args.push_back("a1");
        d.set_arg0("a0");
        EXPECT_STREQ("a0", d.arg0().c_str());
        EXPECT_TRUE(d.arg1().empty());
        EXPECT_TRUE(d.arg2().empty());
        EXPECT_TRUE(d.arg3().empty());
    }

    if (true) {
        SrsConfDirective d;

        SrsConfDirective* vhost = d.get_or_create("vhost");
        d.remove(vhost);
        srs_freep(vhost);

        EXPECT_TRUE(d.get("vhost") == NULL);
    }
}

extern void set_config_directive(SrsConfDirective* parent, string dir, string value);

VOID TEST(ConfigUnitTest, PersistenceConfig)
{
    srs_error_t err;

    if (true) {
        SrsConfDirective d;
        MockSrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(d.persistence(&fw, 0));
        EXPECT_STREQ("", fw.str().c_str());
    }

    if (true) {
        SrsConfDirective d;
        d.name = "root";
        d.args.push_back("on");

        MockSrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(d.persistence(&fw, 0));
        EXPECT_STREQ("", fw.str().c_str());
    }

    if (true) {
        SrsConfDirective d;
        d.get_or_create("global");

        MockSrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(d.persistence(&fw, 0));
        EXPECT_STREQ("global;\n", fw.str().c_str());
    }

    if (true) {
        SrsConfDirective d;
        d.get_or_create("global", "on");

        MockSrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(d.persistence(&fw, 0));
        EXPECT_STREQ("global on;\n", fw.str().c_str());
    }

    if (true) {
        SrsConfDirective d;
        SrsConfDirective* p = d.get_or_create("global", "on");
        p->get_or_create("child", "100");
        p->get_or_create("sibling", "101");

        MockSrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(d.persistence(&fw, 0));
        EXPECT_STREQ("global on {\n    child 100;\n    sibling 101;\n}\n", fw.str().c_str());
    }

    if (true) {
        SrsConfDirective d;
        SrsConfDirective* p = d.get_or_create("global", "on");
        SrsConfDirective* pp = p->get_or_create("child", "100");
        p->get_or_create("sibling", "101");
        pp->get_or_create("grandson", "200");

        MockSrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(d.persistence(&fw, 0));
        EXPECT_STREQ("global on {\n    child 100 {\n        grandson 200;\n    }\n    sibling 101;\n}\n", fw.str().c_str());
    }

    if (true) {
        SrsConfDirective d;
        set_config_directive(&d, "vhost", "on");

        ASSERT_TRUE(d.get("vhost") != NULL);
        EXPECT_STREQ("on", d.get("vhost")->arg0().c_str());

        set_config_directive(&d, "vhost", "off");

        ASSERT_TRUE(d.get("vhost") != NULL);
        EXPECT_STREQ("off", d.get("vhost")->arg0().c_str());
    }
}

