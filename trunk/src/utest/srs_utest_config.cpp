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
#include <srs_utest_config.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_source.hpp>
#include <srs_core_performance.hpp>

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
    MockSrsConfigBuffer buf("");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(0, (int)conf.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameOnly)
{
    MockSrsConfigBuffer buf("winlin;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("winlin arg0;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("winlin arg0 arg1;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2{dir0;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0 dir_arg0;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0 dir_arg0 {ddir0;}}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0 dir_arg0 {ddir0 ddir_arg0;}}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 arg0;dir1 arg1;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 arg0;dir1 {dir2 arg2;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 arg0 \"str_arg\" 100;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 arg0 \"str_arg space\" 100;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 100 101 102;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 100.01 101.02 102.03;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("#commnets\ndir0 arg0;\n#end-comments");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("#commnets\ndir0 arg0;#inline comments\n#end-comments");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf(" #commnets\ndir0 arg0; #inline comments\n #end-comments");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("#commnets\ndir0 arg0;#inline comments\n#end-comments\ndir1 arg1;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 http://www.ossrs.net/api/v1/versions?level=major;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 rtmp://[server]:[port]/[app]/[stream]_[engine];");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidNoEndOfSubDirective)
{
    MockSrsConfigBuffer buf("dir0 {");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidNoStartOfSubDirective)
{
    MockSrsConfigBuffer buf("dir0 }");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidEmptyName)
{
    MockSrsConfigBuffer buf(";");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidEmptyName2)
{
    MockSrsConfigBuffer buf("{}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidEmptyDirective)
{
    MockSrsConfigBuffer buf("dir0 {}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 {}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("\n\ndir0 {}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 {\n\ndir1 arg0;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 {\n\ndir1 \n\narg0;dir2 arg1;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfigBuffer buf("dir0 {\ndir1 {\ndir2 arg2;\n}\n}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
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
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(""));
}

VOID TEST(ConfigMainTest, ParseMinConf)
{
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF));
    
    vector<string> listens = conf.get_listens();
    EXPECT_EQ(1, (int)listens.size());
    EXPECT_STREQ("1935", listens.at(0).c_str());
}

VOID TEST(ConfigMainTest, ParseInvalidDirective)
{
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse("listens 1935;"));
}

VOID TEST(ConfigMainTest, ParseInvalidDirective2)
{
    MockSrsConfig conf;
    // check error for user not specified the listen directive.
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse("chunk_size 4096;"));
}

VOID TEST(ConfigMainTest, CheckConf_listen)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse("listens 1935;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse("listen 0;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse("listen -1;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse("listen -1935;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_pid)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"pids ./objs/srs.pid;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_chunk_size)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"chunk_size 60000;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"chunk_sizes 60000;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"chunk_size 0;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"chunk_size 1;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"chunk_size 127;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"chunk_size -1;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"chunk_size -4096;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"chunk_size 65537;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_ff_log_dir)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"ff_log_dir ./objs;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"ff_log_dirs ./objs;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_srs_log_level)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"srs_log_level trace;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"srs_log_levels trace;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_srs_log_file)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"srs_log_file ./objs/srs.log;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"srs_log_files ./objs/srs.log;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_daemon)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"daemon on;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"daemons on;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_heartbeat)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeats{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{enabled on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeat{enableds on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{interval 9;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeat{intervals 9;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{url http://127.0.0.1:8085/api/v1/servers;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeat{urls http://127.0.0.1:8085/api/v1/servers;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{device_id 0;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeat{device_ids 0;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{summaries on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeat{summariess on;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_http_api)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"http_api{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_apis{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_api{enableds on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_api{listens 1985;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_stats)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"stats{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"statss{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"stats{network 0;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"stats{networks 0;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"stats{network -100;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"stats{network -1;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"stats{disk sda;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"stats{disks sda;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_http_stream)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"http_stream{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_streams{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_stream{enableds on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_stream{listens 8080;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_stream{dirs ./objs/nginx/html;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhosts{}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_edge)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{mode remote;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{modes remote;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{origin 127.0.0.1:1935 localhost:1935;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{origins 127.0.0.1:1935 localhost:1935;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{token_traverse off;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{token_traverses off;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_dvr)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvrs{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_path ./objs/nginx/html;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_paths ./objs/nginx/html;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_plan session;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_plans session;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_duration 30;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_durations 30;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_wait_keyframe on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_wait_keyframes on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{time_jitter full;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{time_jitters full;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_ingest)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingests{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest{input{}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest{inputs{}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest{ffmpeg ./objs/ffmpeg/bin/ffmpeg;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest{ffmpegs ./objs/ffmpeg/bin/ffmpeg;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest{engine{}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest{engines{}}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_http)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{https{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http{mount /hls;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http{mounts /hls;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http{dir ./objs/nginx/html/hls;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http{dirs ./objs/nginx/html/hls;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_hls)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{hls{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{hlss{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{hls{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{hls{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{hls{hls_path ./objs/nginx/html;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{hls{hls_paths ./objs/nginx/html;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{hls{hls_fragment 10;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{hls{hls_fragments 10;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{hls{hls_window 60;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{hls{hls_windows 60;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_hooks)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hookss{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_connect http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_connects http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_close http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_closes http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_publish http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_publishs http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_unpublish http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_unpublishs http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_play http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_plays http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_stop http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_stops http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_gop_cache)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{gop_cache off;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{gop_caches off;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{queue_length 10;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{queue_lengths 10;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_debug_srs_upnode)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{debug_srs_upnode off;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{debug_srs_upnodes off;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_refer)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{refer github.com github.io;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{refers github.com github.io;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{refer_publish github.com github.io;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{refer_publishs github.com github.io;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{refer_play github.com github.io;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{refer_plays github.com github.io;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_forward)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{forward 127.0.0.1:1936;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{forwards 127.0.0.1:1936;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_transcode)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcodes{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{ffmpeg ./objs/ffmpeg/bin/ffmpeg;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{ffmpegs ./objs/ffmpeg/bin/ffmpeg;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engines {}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {enabled on;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {enableds on;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfilter {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfilters {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vcodec libx264;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vcodecs libx264;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vbitrate 300;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vbitrates 300;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfps 20;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfpss 20;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vwidth 768;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vwidths 768;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vheight 320;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vheights 320;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vthreads 2;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vthreadss 2;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vprofile baseline;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vprofiles baseline;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vpreset superfast;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vpresets superfast;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vparams {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vparamss {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {acodec libfdk_aac;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {acodecs libfdk_aac;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {abitrate 45;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {abitrates 45;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {asample_rate 44100;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {asample_rates 44100;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {achannels 2;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {achannelss 2;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {aparams {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {aparamss {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {output rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {outputs rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];}}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_bandcheck)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{bandcheck{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{bandchecks{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{bandcheck{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{bandcheck{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{bandcheck{key \"35c9b402c12a7246868752e2878f7e0e\";}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{bandcheck{keys \"35c9b402c12a7246868752e2878f7e0e\";}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{bandcheck{interval 30;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{bandcheck{intervals 30;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{bandcheck{limit_kbps 4000;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{bandcheck{limit_kbpss 4000;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_chunk_size2)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{chunk_size 128;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{chunk_sizes 128;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{chunk_size 127;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{chunk_size 0;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{chunk_size -1;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{chunk_size -128;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{chunk_size 65537;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_jitter)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{time_jitter full;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{time_jitters full;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_atc)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{atc on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{atcs on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{atc_auto on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{atc_autos on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{enabled on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{enableds on;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_pithy_print)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"pithy_print_ms 1000;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"pithy_print_mss 1000;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_ingest_id)
{
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest id{}}"));
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest id{} ingest id{}}"));
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest{} ingest{}}"));
}

VOID TEST(ConfigUnitTest, CheckDefaultValues)
{
    MockSrsConfig conf;
    if (true) {
	    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_bw_check_interval(""));

	    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{bandcheck{interval 4;}}"));
	    EXPECT_EQ(4 * SRS_UTIME_SECONDS, conf.get_bw_check_interval("v"));
    }

    if (true) {
	    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF));
	    EXPECT_EQ(3 * SRS_UTIME_SECONDS, conf.get_dash_fragment(""));

	    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dash{dash_fragment 4;}}"));
	    EXPECT_EQ(4 * SRS_UTIME_SECONDS, conf.get_dash_fragment("v"));
    }
}

