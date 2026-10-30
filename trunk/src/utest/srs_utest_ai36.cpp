//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai36.hpp>

#include <srs_app_config.hpp>
#include <srs_app_dash.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_edge.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_coworkers.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_http_static.hpp>
#include <srs_app_log.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_ingest.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_server.hpp>
#include <srs_app_srt_conn.hpp>
#include <srs_app_utility.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_conn.hpp>
#endif
#include <srs_app_srt_listener.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_core_platform.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai17.hpp>
#include <srs_utest_ai29.hpp>
#include <srs_utest_ai34.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_service.hpp>

#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

using namespace std;

extern bool _srs_in_docker;
extern srs_error_t srs_config_dumps_engine(SrsConfDirective *dir, SrsJsonObject *engine);

// Writes the content to a new temporary file, and removes it when the test ends.
class TmpConfFileForAi36
{
public:
    string path_;

public:
    TmpConfFileForAi36(const string &name, const string &content)
    {
        path_ = _srs_tmp_file_prefix + name;
        FILE *fp = ::fopen(path_.c_str(), "wb");
        if (fp) {
            ::fwrite(content.data(), 1, content.length(), fp);
            ::fclose(fp);
        }
    }
    ~TmpConfFileForAi36()
    {
        ::unlink(path_.c_str());
    }
};

static string ai36_read_file(const string &path)
{
    string data;
    FILE *fp = ::fopen(path.c_str(), "rb");
    if (!fp) {
        return data;
    }

    char buf[256];
    size_t n;
    while ((n = ::fread(buf, 1, sizeof(buf), fp)) > 0) {
        data.append(buf, n);
    }
    ::fclose(fp);
    return data;
}

VOID TEST(ConfigParseArgvTest, EachOptionSetsItsFlag)
{
    srs_error_t err;

    // Help by -h, -? and an argument that is not an option.
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-h", NULL};
        int i = 1;
        HELPER_EXPECT_SUCCESS(conf.parse_argv(i, argv));
        EXPECT_TRUE(conf.show_help_);
    }
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-?", NULL};
        int i = 1;
        HELPER_EXPECT_SUCCESS(conf.parse_argv(i, argv));
        EXPECT_TRUE(conf.show_help_);
    }
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"conf/srs.conf", NULL};
        int i = 1;
        HELPER_EXPECT_SUCCESS(conf.parse_argv(i, argv));
        EXPECT_TRUE(conf.show_help_);
    }

    // Test the config, and env only.
    if (true) {
        SrsConfig conf;
        conf.show_help_ = true;
        char *argv[] = {(char *)"srs", (char *)"-te", NULL};
        int i = 1;
        HELPER_EXPECT_SUCCESS(conf.parse_argv(i, argv));
        EXPECT_FALSE(conf.show_help_);
        EXPECT_TRUE(conf.test_conf_);
        EXPECT_TRUE(conf.env_only_);
    }

    // Version by -v and -V, which stops parsing the rest of the argument.
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-vt", NULL};
        int i = 1;
        HELPER_EXPECT_SUCCESS(conf.parse_argv(i, argv));
        EXPECT_TRUE(conf.show_version_);
        EXPECT_FALSE(conf.test_conf_);
    }
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-V", NULL};
        int i = 1;
        HELPER_EXPECT_SUCCESS(conf.parse_argv(i, argv));
        EXPECT_TRUE(conf.show_version_);
    }

    // Signature by -g and -G.
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-gG", NULL};
        int i = 1;
        HELPER_EXPECT_SUCCESS(conf.parse_argv(i, argv));
        EXPECT_TRUE(conf.show_signature_);
        EXPECT_FALSE(conf.show_help_);
    }

    // The long --help is a dash followed by the h option.
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"--help", NULL};
        int i = 1;
        HELPER_EXPECT_SUCCESS(conf.parse_argv(i, argv));
        EXPECT_TRUE(conf.show_help_);
    }
}

VOID TEST(ConfigParseArgvTest, ConfigFileOption)
{
    srs_error_t err;

    // The file in the next argument, which the parser consumes.
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-c", (char *)"bar.conf", NULL};
        int i = 1;
        HELPER_EXPECT_SUCCESS(conf.parse_argv(i, argv));
        EXPECT_STREQ("bar.conf", conf.config_file_.c_str());
        EXPECT_EQ(2, i);
    }

    // No file after -c.
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-c", NULL};
        int i = 1;
        HELPER_EXPECT_FAILED(conf.parse_argv(i, argv));
    }

    // An unknown option.
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-x", NULL};
        int i = 1;
        err = conf.parse_argv(i, argv);
        EXPECT_EQ(ERROR_SYSTEM_CONFIG_INVALID, srs_error_code(err));
        srs_freep(err);
    }
}

VOID TEST(ConfigPrintHelpTest, PrintsUsageWithProgramName)
{
    SrsConfig conf;
    char *argv[] = {(char *)"./objs/srs", NULL};

    testing::internal::CaptureStdout();
    conf.print_help(argv);
    string out = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(out.find("Usage: ./objs/srs") != string::npos) << out;
    EXPECT_TRUE(out.find("-c filename") != string::npos) << out;
    EXPECT_TRUE(out.find("SRS_HEARTBEAT_AUTH_TOKEN") != string::npos) << out;
}

VOID TEST(ConfigParseOptionsTest, FailsAtInvalidOption)
{
    SrsConfig conf;
    char *argv[] = {(char *)"srs", (char *)"-x", NULL};
    srs_error_t err = conf.parse_options(2, argv);
    EXPECT_EQ(ERROR_SYSTEM_CONFIG_INVALID, srs_error_code(err));
    srs_freep(err);
    EXPECT_STREQ("srs -x", conf.argv_.c_str());
}

VOID TEST(ConfigParseOptionsTest, FailsWithoutConfigFile)
{
    SrsConfig conf;
    string path = _srs_tmp_file_prefix + "ai36-no-such.conf";
    char *argv[] = {(char *)"srs", (char *)"-c", (char *)path.c_str(), NULL};
    srs_error_t err = conf.parse_options(3, argv);
    EXPECT_EQ(ERROR_SYSTEM_CONFIG_INVALID, srs_error_code(err));
    srs_freep(err);
}

VOID TEST(ConfigParseOptionsTest, FailsAtInvalidConfig)
{
    TmpConfFileForAi36 f("ai36-invalid.conf", "rtmp { listen 1935;");

    SrsConfig conf;
    char *argv[] = {(char *)"srs", (char *)"-c", (char *)f.path_.c_str(), NULL};
    srs_error_t err = conf.parse_options(3, argv);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// SRS_CONFIG_FILE replaces the file of -c, so a missing -c file is not an error.
VOID TEST(ConfigParseOptionsTest, ConfigFileFromEnv)
{
    srs_error_t err;

    TmpConfFileForAi36 f("ai36-env.conf", _MIN_OK_CONF "max_connections 123;");

    SrsConfig conf;
    SrsSetEnvConfig(conf, config_file, "SRS_CONFIG_FILE", f.path_);
    SrsSetEnvConfig(conf, log_tank, "SRS_LOG_TANK", "file");

    bool in_docker = _srs_in_docker;
    char *argv[] = {(char *)"srs", (char *)"-c", (char *)"ai36-no-such.conf", NULL};
    err = conf.parse_options(3, argv);
    _srs_in_docker = in_docker;

    HELPER_EXPECT_SUCCESS(err);
    EXPECT_STREQ(f.path_.c_str(), conf.config().c_str());
    EXPECT_EQ(123, conf.get_max_connections());
}

// SRS_ENV_ONLY ignores the config file, and SRS_CONFIG_FILE too.
VOID TEST(ConfigParseOptionsTest, EnvOnlyFromEnv)
{
    srs_error_t err;

    SrsConfig conf;
    SrsSetEnvConfig(conf, env_only, "SRS_ENV_ONLY", "on");
    SrsSetEnvConfig(conf, config_file, "SRS_CONFIG_FILE", "ai36-no-such.conf");
    SrsSetEnvConfig(conf, log_tank, "SRS_LOG_TANK", "console");

    bool in_docker = _srs_in_docker;
    char *argv[] = {(char *)"srs", (char *)"-c", (char *)"ai36-no-such-either.conf", NULL};
    err = conf.parse_options(3, argv);
    _srs_in_docker = in_docker;

    HELPER_EXPECT_SUCCESS(err);
    EXPECT_TRUE(conf.env_only_);
    EXPECT_STREQ("ai36-no-such-either.conf", conf.config().c_str());
    EXPECT_TRUE(conf.get_vhost("__defaultVhost__", false) != NULL);
}

#ifndef _WIN32 // POSIX only: the death tests fork
VOID TEST(ConfigParseOptionsDeathTest, ExitsAfterHelpVersionOrSignature)
{
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", NULL};
        EXPECT_EXIT(conf.parse_options(1, argv), ::testing::ExitedWithCode(0), "");
    }
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-v", NULL};
        EXPECT_EXIT(conf.parse_options(2, argv), ::testing::ExitedWithCode(0), "");
    }
    if (true) {
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-g", NULL};
        EXPECT_EXIT(conf.parse_options(2, argv), ::testing::ExitedWithCode(0), "");
    }
}

VOID TEST(ConfigParseOptionsDeathTest, TestConfigExitsWithItsResult)
{
    if (true) {
        TmpConfFileForAi36 f("ai36-test-ok.conf", _MIN_OK_CONF);
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-t", (char *)"-c", (char *)f.path_.c_str(), NULL};
        EXPECT_EXIT(conf.parse_options(4, argv), ::testing::ExitedWithCode(0), "");
    }

    // A config that parses but fails the check exits with the error code, truncated to a byte.
    if (true) {
        TmpConfFileForAi36 f("ai36-test-bad.conf", _MIN_OK_CONF "max_connections 0;");
        SrsConfig conf;
        char *argv[] = {(char *)"srs", (char *)"-t", (char *)"-c", (char *)f.path_.c_str(), NULL};
        EXPECT_EXIT(conf.parse_options(4, argv), ::testing::ExitedWithCode(ERROR_SYSTEM_CONFIG_INVALID & 0xff), "");
    }
}
#endif

VOID TEST(ConfigParseFileTest, FailsForEmptyOrMissingFile)
{
    if (true) {
        SrsConfig conf;
        srs_error_t err = conf.parse_file("");
        EXPECT_EQ(ERROR_SYSTEM_CONFIG_INVALID, srs_error_code(err));
        srs_freep(err);
    }

    if (true) {
        SrsConfig conf;
        string path = _srs_tmp_file_prefix + "ai36-missing.conf";
        srs_error_t err = conf.parse_file(path.c_str());
        EXPECT_TRUE(err != srs_success);
        EXPECT_TRUE(srs_error_desc(err).find("buffer fullfill") != string::npos);
        srs_freep(err);
    }

    if (true) {
        TmpConfFileForAi36 f("ai36-unclosed.conf", "rtmp { listen 1935;");
        SrsConfig conf;
        srs_error_t err = conf.parse_file(f.path_.c_str());
        EXPECT_TRUE(err != srs_success);
        EXPECT_TRUE(srs_error_desc(err).find("parse buffer") != string::npos);
        srs_freep(err);
    }
}

VOID TEST(ConfigParseFileTest, ParsesFileOnDisk)
{
    srs_error_t err;

    TmpConfFileForAi36 f("ai36-ok.conf", _MIN_OK_CONF "max_connections 321;");
    SrsConfig conf;
    HELPER_EXPECT_SUCCESS(conf.parse_file(f.path_.c_str()));
    EXPECT_EQ(321, conf.get_max_connections());
    EXPECT_STREQ(f.path_.c_str(), conf.config().c_str());
}

VOID TEST(ConfigReloadTest, StateTellsWhereReloadStopped)
{
    srs_error_t err;

    // The file is gone, so the reload stops at parsing.
    if (true) {
        SrsConfig conf;
        conf.config_file_ = _srs_tmp_file_prefix + "ai36-reload-missing.conf";
        SrsReloadState state = SrsReloadStateInit;
        err = conf.reload(&state);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
        EXPECT_EQ(SrsReloadStateParsing, state);
    }

    // The file parses, but fails the check.
    if (true) {
        TmpConfFileForAi36 f("ai36-reload-bad.conf", _MIN_OK_CONF "max_connections 0;");
        SrsConfig conf;
        conf.config_file_ = f.path_;
        SrsReloadState state = SrsReloadStateInit;
        err = conf.reload(&state);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
        EXPECT_EQ(SrsReloadStateTransforming, state);
    }

    if (true) {
        TmpConfFileForAi36 f("ai36-reload-ok.conf", _MIN_OK_CONF);
        SrsConfig conf;
        conf.config_file_ = f.path_;
        SrsReloadState state = SrsReloadStateInit;
        HELPER_EXPECT_SUCCESS(conf.reload(&state));
        EXPECT_EQ(SrsReloadStateFinished, state);
    }
}

VOID TEST(ConfigCheckTest, NumberConnections)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "max_connections 100;"));
        HELPER_EXPECT_SUCCESS(conf.check_number_connections());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "max_connections 0;"));
        err = conf.check_number_connections();
        EXPECT_EQ(ERROR_SYSTEM_CONFIG_INVALID, srs_error_code(err));
        srs_freep(err);
    }

    // More than the system allows for open files.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "max_connections 2000000000;"));
        err = conf.check_number_connections();
        EXPECT_EQ(ERROR_SYSTEM_CONFIG_INVALID, srs_error_code(err));
        srs_freep(err);
    }
}

VOID TEST(ConfigPersistenceTest, WritesConfigThatParsesBack)
{
    srs_error_t err;

    string path = _srs_tmp_file_prefix + "ai36-persist.conf";
    MockFileRemover mfr(path);

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "max_connections 456;"));
    conf.config_file_ = path;
    HELPER_EXPECT_SUCCESS(conf.persistence());

    string content = ai36_read_file(path);
    EXPECT_TRUE(content.find("max_connections 456;") != string::npos) << content;

    SrsPath p;
    EXPECT_FALSE(p.exists(path + ".tmp"));

    SrsConfig reloaded;
    HELPER_EXPECT_SUCCESS(reloaded.parse_file(path.c_str()));
    EXPECT_EQ(456, reloaded.get_max_connections());
}

VOID TEST(ConfigPersistenceTest, FailsToOpenOrRename)
{
    srs_error_t err;

    // The directory of the config does not exist.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        conf.config_file_ = _srs_tmp_file_prefix + "ai36-no-dir/srs.conf";
        HELPER_EXPECT_FAILED(conf.persistence());
    }

    // The config is a directory, so the temporary file cannot replace it, and is removed.
    if (true) {
        string dir = _srs_tmp_file_prefix + "ai36-persist-dir";
        SrsPath p;
        HELPER_ASSERT_SUCCESS(p.mkdir_all(dir));

        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        conf.config_file_ = dir;
        err = conf.persistence();
        EXPECT_EQ(ERROR_SYSTEM_CONFIG_PERSISTENCE, srs_error_code(err));
        srs_freep(err);
        EXPECT_FALSE(p.exists(dir + ".tmp"));

        ::rmdir(dir.c_str());
    }
}

VOID TEST(ConfigRawApiTest, RawToJsonReportsHttpApi)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                          "http_api { enabled on; listen 9985; crossdomain off;"
                                          " raw_api { enabled on; allow_reload on; allow_query on; allow_update off; } }"));

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(conf.raw_to_json(obj.get()));

    SrsJsonAny *api = obj->get_property("http_api");
    ASSERT_TRUE(api != NULL && api->is_object());
    SrsJsonObject *sobj = api->to_object();
    EXPECT_TRUE(sobj->get_property("enabled")->to_boolean());
    EXPECT_STREQ("9985", sobj->get_property("listen")->to_str().c_str());
    EXPECT_FALSE(sobj->get_property("crossdomain")->to_boolean());

    SrsJsonAny *raw = sobj->get_property("raw_api");
    ASSERT_TRUE(raw != NULL && raw->is_object());
    SrsJsonObject *ssobj = raw->to_object();
    EXPECT_TRUE(ssobj->get_property("enabled")->to_boolean());
    EXPECT_TRUE(ssobj->get_property("allow_reload")->to_boolean());
    // Query and update are always off, whatever the config says.
    EXPECT_FALSE(ssobj->get_property("allow_query")->to_boolean());
    EXPECT_FALSE(ssobj->get_property("allow_update")->to_boolean());
}

VOID TEST(ConfigCwdTest, InitializeCwdIsTheWorkingDirectory)
{
    SrsConfig conf;
    srs_error_t err = conf.initialize_cwd();
    HELPER_EXPECT_SUCCESS(err);

    char cwd[256];
    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    EXPECT_STREQ(cwd, conf.cwd_.c_str());
}

VOID TEST(ConfDirectiveDumpsTest, ArgsAndArg0Conversions)
{
    SrsConfDirective dir;
    dir.name_ = "x";
    dir.args_.push_back("12.5");
    dir.args_.push_back("b");

    SrsUniquePtr<SrsJsonArray> arr(dir.dumps_args());
    ASSERT_EQ(2, arr->count());
    EXPECT_STREQ("12.5", arr->at(0)->to_str().c_str());
    EXPECT_STREQ("b", arr->at(1)->to_str().c_str());

    SrsUniquePtr<SrsJsonAny> s(dir.dumps_arg0_to_str());
    EXPECT_STREQ("12.5", s->to_str().c_str());

    SrsUniquePtr<SrsJsonAny> i(dir.dumps_arg0_to_integer());
    EXPECT_EQ(12, i->to_integer());

    SrsUniquePtr<SrsJsonAny> n(dir.dumps_arg0_to_number());
    EXPECT_DOUBLE_EQ(12.5, n->to_number());

    SrsUniquePtr<SrsJsonAny> b(dir.dumps_arg0_to_boolean());
    EXPECT_FALSE(b->to_boolean());

    SrsConfDirective on;
    on.args_.push_back("on");
    SrsUniquePtr<SrsJsonAny> b2(on.dumps_arg0_to_boolean());
    EXPECT_TRUE(b2->to_boolean());
}

VOID TEST(ConfigDumpsEngineTest, DumpsEveryEngineField)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                          "vhost v { transcode { enabled on; ffmpeg ffmpeg; engine e1 {"
                                          " enabled on; iformat flv; vfilter { i logo.png; } vcodec libx264;"
                                          " vbitrate 300; vfps 20.5; vwidth 768; vheight 320; vthreads 2;"
                                          " vprofile main; vpreset medium; vparams { t 100; } acodec libfdk_aac;"
                                          " abitrate 45; asample_rate 44100; achannels 2; aparams { profile:a aac_low; }"
                                          " oformat flv; output rtmp://127.0.0.1/live/s_e1; } } }"));

    SrsConfDirective *vhost = conf.get_vhost("v");
    ASSERT_TRUE(vhost != NULL);
    SrsConfDirective *engine = vhost->get("transcode")->get("engine");
    ASSERT_TRUE(engine != NULL);

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(srs_config_dumps_engine(engine, obj.get()));

    EXPECT_STREQ("e1", obj->get_property("id")->to_str().c_str());
    EXPECT_TRUE(obj->get_property("enabled")->to_boolean());
    EXPECT_STREQ("flv", obj->get_property("iformat")->to_str().c_str());
    EXPECT_STREQ("logo.png", obj->get_property("vfilter")->to_object()->get_property("i")->to_str().c_str());
    EXPECT_STREQ("libx264", obj->get_property("vcodec")->to_str().c_str());
    EXPECT_EQ(300, obj->get_property("vbitrate")->to_integer());
    EXPECT_DOUBLE_EQ(20.5, obj->get_property("vfps")->to_number());
    EXPECT_EQ(768, obj->get_property("vwidth")->to_integer());
    EXPECT_EQ(320, obj->get_property("vheight")->to_integer());
    EXPECT_EQ(2, obj->get_property("vthreads")->to_integer());
    EXPECT_STREQ("main", obj->get_property("vprofile")->to_str().c_str());
    EXPECT_STREQ("medium", obj->get_property("vpreset")->to_str().c_str());
    EXPECT_STREQ("100", obj->get_property("vparams")->to_object()->get_property("t")->to_str().c_str());
    EXPECT_STREQ("libfdk_aac", obj->get_property("acodec")->to_str().c_str());
    EXPECT_EQ(45, obj->get_property("abitrate")->to_integer());
    EXPECT_EQ(44100, obj->get_property("asample_rate")->to_integer());
    EXPECT_EQ(2, obj->get_property("achannels")->to_integer());
    EXPECT_STREQ("aac_low", obj->get_property("aparams")->to_object()->get_property("profile:a")->to_str().c_str());
    EXPECT_STREQ("flv", obj->get_property("oformat")->to_str().c_str());
    EXPECT_STREQ("rtmp://127.0.0.1/live/s_e1", obj->get_property("output")->to_str().c_str());
}

// An engine with only its id dumps no optional field.
VOID TEST(ConfigDumpsEngineTest, DumpsOnlyPresentFields)
{
    srs_error_t err;

    SrsConfDirective engine;
    engine.name_ = "engine";
    engine.args_.push_back("e2");

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(srs_config_dumps_engine(&engine, obj.get()));
    EXPECT_EQ(2, obj->count());
    EXPECT_STREQ("e2", obj->get_property("id")->to_str().c_str());
    EXPECT_TRUE(obj->get_property("vcodec") == NULL);
}

// The factory creates the real class behind each interface.
VOID TEST(AppFactoryTest, CreatesRealClasses)
{
    SrsAppFactory factory;

    if (true) {
        SrsUniquePtr<ISrsFileWriter> o(factory.create_enc_file_writer());
        EXPECT_TRUE(dynamic_cast<SrsEncFileWriter *>(o.get()) != NULL);
    }
    if (true) {
        SrsUniquePtr<ISrsFileReader> o(factory.create_file_reader());
        EXPECT_TRUE(dynamic_cast<SrsFileReader *>(o.get()) != NULL);
    }
    if (true) {
        SrsUniquePtr<ISrsHttpClient> o(factory.create_http_client());
        EXPECT_TRUE(dynamic_cast<SrsHttpClient *>(o.get()) != NULL);
    }
    if (true) {
        SrsUniquePtr<ISrsFileReader> o(factory.create_http_file_reader(NULL));
        EXPECT_TRUE(dynamic_cast<SrsHttpFileReader *>(o.get()) != NULL);
    }
    if (true) {
        SrsUniquePtr<ISrsFlvDecoder> o(factory.create_flv_decoder());
        EXPECT_TRUE(dynamic_cast<SrsFlvDecoder *>(o.get()) != NULL);
    }
    if (true) {
        SrsUniquePtr<ISrsMp4Encoder> o(factory.create_mp4_encoder());
        EXPECT_TRUE(dynamic_cast<SrsMp4Encoder *>(o.get()) != NULL);
    }
    if (true) {
        SrsUniquePtr<ISrsDvrSegmenter> o(factory.create_dvr_mp4_segmenter());
        EXPECT_TRUE(dynamic_cast<SrsDvrMp4Segmenter *>(o.get()) != NULL);
    }
    if (true) {
        SrsUniquePtr<ISrsInitMp4> o(factory.create_init_mp4());
        EXPECT_TRUE(dynamic_cast<SrsInitMp4 *>(o.get()) != NULL);
    }
    if (true) {
        SrsUniquePtr<ISrsFragmentedMp4> o(factory.create_fragmented_mp4());
        EXPECT_TRUE(dynamic_cast<SrsFragmentedMp4 *>(o.get()) != NULL);
    }
    if (true) {
        SrsUniquePtr<ISrsSrtListener> o(factory.create_srt_listener(NULL, "127.0.0.1", 10080));
        SrsSrtListener *l = dynamic_cast<SrsSrtListener *>(o.get());
        ASSERT_TRUE(l != NULL);
        EXPECT_STREQ("127.0.0.1", l->ip_.c_str());
        EXPECT_EQ(10080, l->port_);
    }
    if (true) {
        SrsUniquePtr<ISrsSrtAcceptor> o(factory.create_srt_acceptor(NULL));
        EXPECT_TRUE(dynamic_cast<SrsSrtAcceptor *>(o.get()) != NULL);
    }
    if (true) {
        SrsUniquePtr<ISrsFFMPEG> o(factory.create_ffmpeg("/usr/bin/ffmpeg"));
        SrsFFMPEG *f = dynamic_cast<SrsFFMPEG *>(o.get());
        ASSERT_TRUE(f != NULL);
        EXPECT_STREQ("/usr/bin/ffmpeg", f->ffmpeg_.c_str());
    }
    if (true) {
        SrsUniquePtr<ISrsIngesterFFMPEG> o(factory.create_ingester_ffmpeg());
        EXPECT_TRUE(dynamic_cast<SrsIngesterFFMPEG *>(o.get()) != NULL);
    }
    if (true) {
        SrsUniquePtr<ISrsProtocolUtility> o(factory.create_protocol_utility());
        EXPECT_TRUE(dynamic_cast<SrsProtocolUtility *>(o.get()) != NULL);
    }
}

VOID TEST(AppFactoryTest, CreatesRtcConnectionAndPublishStream)
{
    SrsAppFactory factory;
    SrsContextId cid;
    cid.set_value("ai36-rtc");

    if (true) {
        SrsUniquePtr<ISrsRtcConnection> o(factory.create_rtc_connection(NULL, cid));
        SrsRtcConnection *c = dynamic_cast<SrsRtcConnection *>(o.get());
        ASSERT_TRUE(c != NULL);
        EXPECT_STREQ("ai36-rtc", c->get_id().c_str());
    }
    if (true) {
        SrsUniquePtr<ISrsRtcPublishStream> o(factory.create_rtc_publish_stream(NULL, NULL, NULL, cid));
        EXPECT_TRUE(dynamic_cast<SrsRtcPublishStream *>(o.get()) != NULL);
    }
}

VOID TEST(ConfigProxyTest, ReadsGlobalConfig)
{
    SrsConfigProxy proxy;
    EXPECT_STREQ(_srs_config->get_default_app_name().c_str(), proxy.get_default_app_name().c_str());
    EXPECT_STREQ(_srs_config->get_srt_default_mode().c_str(), proxy.get_srt_default_mode().c_str());
}

MockAppConfigForEmptyIp::MockAppConfigForEmptyIp()
{
    empty_ip_ok_ = false;
}

MockAppConfigForEmptyIp::~MockAppConfigForEmptyIp()
{
}

bool MockAppConfigForEmptyIp::empty_ip_ok()
{
    return empty_ip_ok_;
}

MockConnectionManagerForServerQuit::MockConnectionManagerForServerQuit()
{
    nn_not_empty_ = 0;
    empty_count_ = 0;
    start_error_ = srs_success;
}

MockConnectionManagerForServerQuit::~MockConnectionManagerForServerQuit()
{
    srs_freep(start_error_);
}

srs_error_t MockConnectionManagerForServerQuit::start()
{
    srs_error_t err = start_error_;
    start_error_ = srs_success;
    return err;
}

bool MockConnectionManagerForServerQuit::empty()
{
    empty_count_++;
    if (nn_not_empty_ > 0) {
        nn_not_empty_--;
        return false;
    }
    return true;
}

VOID TEST(ServerListenTest, StartsSecureRtcAndRtspListeners)
{
    srs_error_t err;

    MockAppConfigForServerListen config;
    config.rtmp_listens_.push_back("1935");
    config.rtmps_enabled_ = true;
    config.rtmps_listens_.push_back("1443");
    config.https_api_enabled_ = true;
    config.https_api_listens_.push_back("1990");
    config.https_stream_enabled_ = true;
    config.https_stream_listens_.push_back("8088");
    config.rtc_server_protocol_ = "tcp";
    config.rtc_server_tcp_enabled_ = true;
    config.rtc_server_tcp_listens_.push_back("8000");
    config.rtsp_server_enabled_ = true;
    config.rtsp_server_listens_.push_back("554");
    MockConnectionManagerForServerQuit conn_manager;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->config_ = &config;
    server->conn_manager_ = &conn_manager;

    Ai36ServerListenersInjector l(server.get());
    HELPER_EXPECT_SUCCESS(server->listen());

    EXPECT_STREQ("1443", srs_strings_join(l.rtmps_.endpoints_, ",").c_str());
    EXPECT_STREQ("RTMPS", l.rtmps_.label_.c_str());
    EXPECT_EQ(1, l.rtmps_.listen_count_);
    EXPECT_STREQ("1990", srs_strings_join(l.apis_.endpoints_, ",").c_str());
    EXPECT_STREQ("HTTPS-API", l.apis_.label_.c_str());
    EXPECT_EQ(1, l.apis_.listen_count_);
    EXPECT_STREQ("8088", srs_strings_join(l.https_.endpoints_, ",").c_str());
    EXPECT_STREQ("HTTPS-Server", l.https_.label_.c_str());
    EXPECT_EQ(1, l.https_.listen_count_);
    EXPECT_STREQ("8000", srs_strings_join(l.webrtc_.endpoints_, ",").c_str());
    EXPECT_STREQ("WebRTC", l.webrtc_.label_.c_str());
    EXPECT_EQ(1, l.webrtc_.listen_count_);
#ifdef SRS_RTSP
    EXPECT_STREQ("554", srs_strings_join(l.rtsp_.endpoints_, ",").c_str());
    EXPECT_STREQ("RTSP", l.rtsp_.label_.c_str());
    EXPECT_EQ(1, l.rtsp_.listen_count_);
#endif

    server->config_ = NULL;
    server->conn_manager_ = NULL;
}

// When the API reuses the HTTP server, no API listener is started.
VOID TEST(ServerListenTest, ApiOverServerStartsNoApiListener)
{
    srs_error_t err;

    MockAppConfigForServerListen config;
    config.rtmp_listens_.push_back("1935");
    config.http_api_enabled_ = true;
    config.http_api_listens_.push_back("8080");
    config.https_api_enabled_ = true;
    config.https_api_listens_.push_back("8088");
    config.http_stream_enabled_ = true;
    config.http_stream_listens_.push_back("8080");
    config.https_stream_enabled_ = true;
    config.https_stream_listens_.push_back("8088");
    MockConnectionManagerForServerQuit conn_manager;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->config_ = &config;
    server->conn_manager_ = &conn_manager;
    bool reuse = server->reuse_api_over_server_;
    server->reuse_api_over_server_ = true;

    Ai36ServerListenersInjector l(server.get());
    HELPER_EXPECT_SUCCESS(server->listen());

    EXPECT_EQ(0, l.api_.add_count_ + l.api_.listen_count_);
    EXPECT_EQ(0, l.apis_.add_count_ + l.apis_.listen_count_);
    EXPECT_EQ(1, l.http_.listen_count_);
    EXPECT_EQ(1, l.https_.listen_count_);

    server->reuse_api_over_server_ = reuse;
    server->config_ = NULL;
    server->conn_manager_ = NULL;
}

// Each listener that fails to start fails the server with its own error.
VOID TEST(ServerListenTest, FailsAtEachListener)
{
    srs_error_t err;

    SrsUniquePtr<SrsConfDirective> mpegts_conf(mock_server_stream_caster_conf("mpegts_over_udp", true));

    MockAppConfigForServerListen config;
    config.rtmp_listens_.push_back("1935");
    config.rtmps_enabled_ = true;
    config.http_api_enabled_ = true;
    config.https_api_enabled_ = true;
    config.http_stream_enabled_ = true;
    config.https_stream_enabled_ = true;
    config.rtc_server_protocol_ = "all";
    config.rtc_server_tcp_enabled_ = true;
    config.rtsp_server_enabled_ = true;
    config.exporter_enabled_ = true;
    config.exporter_listen_ = "9972";
    config.stream_casters_.push_back(mpegts_conf.get());
    MockConnectionManagerForServerQuit conn_manager;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->config_ = &config;
    server->conn_manager_ = &conn_manager;

    Ai36ServerListenersInjector l(server.get());
    MockTcpListenersForServer *tcps[] = {&l.rtmps_, &l.api_, &l.apis_, &l.http_, &l.https_, &l.webrtc_,
#ifdef SRS_RTSP
                                         &l.rtsp_
#endif
    };
    const char *descs[] = {"rtmps listen", "http api listen", "https api listen", "http server listen",
                           "https server listen", "webrtc tcp listen", "rtsp listen"};
    for (int i = 0; i < (int)(sizeof(tcps) / sizeof(tcps[0])); i++) {
        tcps[i]->listen_error_ = srs_error_new(ERROR_SOCKET_BIND, "mock bind");
        err = server->listen();
        EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
        EXPECT_TRUE(srs_error_desc(err).find(descs[i]) != string::npos) << descs[i];
        srs_freep(err);
    }

    l.mpegts_.listen_error_ = srs_error_new(ERROR_SOCKET_BIND, "mock caster");
    err = server->listen();
    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    srs_freep(err);

    l.exporter_.listen_error_ = srs_error_new(ERROR_SOCKET_BIND, "mock exporter");
    err = server->listen();
    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("exporter server listen") != string::npos);
    srs_freep(err);

    conn_manager.start_error_ = srs_error_new(ERROR_ST_INITIALIZE, "mock manager");
    err = server->listen();
    EXPECT_EQ(ERROR_ST_INITIALIZE, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("connection manager") != string::npos);
    srs_freep(err);

    server->config_ = NULL;
    server->conn_manager_ = NULL;
}

VOID TEST(ServerListenTest, FailsAtSrtOrRtcUdp)
{
    srs_error_t err;

    // No SRT acceptor listens.
    if (true) {
        MockAppFactoryForServerSrt factory;
        factory.fail_at_.push_back(0);
        MockAppConfigForServerListen config;
        config.srt_enabled_ = true;
        config.srt_listens_.push_back("10080");

        SrsUniquePtr<SrsServer> server(new SrsServer());
        server->assemble();
        server->config_ = &config;
        server->app_factory_ = &factory;
        Ai36ServerListenersInjector l(server.get());

        err = server->listen();
        EXPECT_EQ(ERROR_SOCKET_LISTEN, srs_error_code(err));
        EXPECT_TRUE(srs_error_desc(err).find("srt mpegts listen") != string::npos);
        srs_freep(err);

        server->config_ = NULL;
        server->app_factory_ = NULL;
    }

    // The WebRTC UDP listener fails.
    if (true) {
        MockAppFactoryForServerRtcUdp factory;
        factory.fail_at_ = 0;
        MockAppConfigForServerListen config;
        config.rtc_server_enabled_ = true;
        config.rtc_server_listens_.push_back("18000");

        SrsUniquePtr<SrsServer> server(new SrsServer());
        server->assemble();
        server->config_ = &config;
        server->app_factory_ = &factory;
        Ai36ServerListenersInjector l(server.get());

        err = server->listen();
        EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
        EXPECT_TRUE(srs_error_desc(err).find("rtc udp listen") != string::npos);
        srs_freep(err);

        server->config_ = NULL;
        server->app_factory_ = NULL;
    }
}

VOID TEST(ServerListenTest, RtcUdpRefusesEmptyOrInvalidListen)
{
    srs_error_t err;

    MockAppFactoryForServerRtcUdp factory;
    MockAppConfigForServerListen config;
    config.rtc_server_enabled_ = true;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->config_ = &config;
    server->app_factory_ = &factory;

    err = server->listen_rtc_udp();
    EXPECT_EQ(ERROR_RTC_PORT, srs_error_code(err));
    srs_freep(err);

    config.rtc_server_listens_.push_back("0");
    err = server->listen_rtc_udp();
    EXPECT_EQ(ERROR_RTC_PORT, srs_error_code(err));
    srs_freep(err);
    EXPECT_TRUE(factory.listeners_.empty());

    server->config_ = NULL;
    server->app_factory_ = NULL;
}

// A graceful quit closes every listener, stops the ingesters, then waits for the connections to quit.
VOID TEST(ServerGracefulQuitTest, ClosesListenersAndWaitsForConnections)
{
    MockAppConfig config;
    MockConnectionManagerForServerQuit conn_manager;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->config_ = &config;
    server->conn_manager_ = &conn_manager;

    MockIngesterForServer *ingester = new MockIngesterForServer();
    srs_freep(server->ingester_);
    server->ingester_ = ingester;

    Ai36ServerListenersInjector l(server.get());

    // One connection remains for the first check of each loop, then it quits.
    conn_manager.nn_not_empty_ = 2;
    server->gracefully_dispose();

    EXPECT_EQ(1, l.rtmp_.close_count_);
    EXPECT_EQ(1, l.rtmps_.close_count_);
    EXPECT_EQ(1, l.api_.close_count_);
    EXPECT_EQ(1, l.apis_.close_count_);
    EXPECT_EQ(1, l.http_.close_count_);
    EXPECT_EQ(1, l.https_.close_count_);
    EXPECT_EQ(1, l.webrtc_.close_count_);
#ifdef SRS_RTSP
    EXPECT_EQ(1, l.rtsp_.close_count_);
#endif
    EXPECT_EQ(1, l.exporter_.close_count_);
    EXPECT_EQ(1, l.mpegts_.close_count_);
    EXPECT_EQ(1, ingester->stop_count_);
    EXPECT_EQ(3, conn_manager.empty_count_);

    server->config_ = NULL;
    server->conn_manager_ = NULL;
}

// A fast quit while gracefully quitting stops waiting for the connections.
VOID TEST(ServerGracefulQuitTest, FastQuitStopsWaiting)
{
    MockAppConfig config;
    MockConnectionManagerForServerQuit conn_manager;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->config_ = &config;
    server->conn_manager_ = &conn_manager;
    Ai36ServerListenersInjector l(server.get());

    conn_manager.nn_not_empty_ = 1000;
    server->signal_fast_quit_ = true;
    server->gracefully_dispose();

    EXPECT_EQ(1, conn_manager.empty_count_);
    EXPECT_EQ(1, l.rtmp_.close_count_);

    server->config_ = NULL;
    server->conn_manager_ = NULL;
}

VOID TEST(ServerCycleTest, DoCycleEndsAtQuitSignalOrError)
{
    srs_error_t err;

    MockAppConfigForDo2Cycle config;
    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->config_ = &config;

    server->signal_gracefully_quit_ = true;
    HELPER_EXPECT_SUCCESS(server->do_cycle());

    server->signal_gracefully_quit_ = false;
    server->signal_persistence_config_ = true;
    config.persistence_error_ = srs_error_new(ERROR_SYSTEM_CONFIG_PERSISTENCE, "mock persistence");
    err = server->do_cycle();
    EXPECT_EQ(ERROR_SYSTEM_CONFIG_PERSISTENCE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, config.persistence_count_);

    server->config_ = NULL;
}

// Every kind of connection in the manager adds its traffic to the statistic.
VOID TEST(ServerResampleKbpsTest, AddsDeltaOfEachConnectionKind)
{
    MockConnectionManagerForResampleKbps conn_manager;
    MockStatisticForResampleKbps stat;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->conn_manager_ = &conn_manager;
    server->stat_ = &stat;

    SrsRtmpConn *rtmp = new SrsRtmpConn(new SrsRtmpTransport(NULL), "127.0.0.1", 1935);
    rtmp->assemble();
    SrsHttpxConn *httpx = new SrsHttpxConn(&conn_manager, new SrsTcpConnection(NULL), NULL, "127.0.0.1", 8080, "", "");
    httpx->assemble();
    SrsRtcTcpConn *tcp = new SrsRtcTcpConn(new SrsTcpConnection(NULL), "127.0.0.1", 8000);
    tcp->assemble();
    SrsMpegtsSrtConn *srt = new SrsMpegtsSrtConn(&conn_manager, srs_srt_socket_invalid(), "127.0.0.1", 10080);
    srt->assemble();
    SrsAppFactory factory;
    SrsContextId cid;
    ISrsRtcConnection *rtc = factory.create_rtc_connection(NULL, cid.set_value("ai36-resample"));

    vector<ISrsResource *> conns;
    conns.push_back(rtmp);
    conns.push_back(httpx);
    conns.push_back(tcp);
    conns.push_back(srt);
    conns.push_back(dynamic_cast<SrsRtcConnection *>(rtc));
#ifdef SRS_RTSP
    SrsRtspConnection *rtsp = new SrsRtspConnection(&conn_manager, new SrsTcpConnection(NULL), "127.0.0.1", 554);
    rtsp->assemble();
    conns.push_back(rtsp);
#endif
    conn_manager.connections_ = conns;

    server->resample_kbps();
    EXPECT_EQ((int)conns.size(), stat.kbps_add_delta_count_);
    EXPECT_EQ(1, stat.kbps_sample_count_);

    conn_manager.connections_.clear();
    for (int i = 0; i < (int)conns.size(); i++) {
        ISrsResource *c = conns.at(i);
        srs_freep(c);
    }

    server->conn_manager_ = NULL;
    server->stat_ = NULL;
}

// Accepts one client of the server's listener through the acceptor, and returns the client's end.
static SrsTcpConnection *ai36_accept_client(MockServerTcpAcceptor *acceptor, SrsTcpListener *listener)
{
    int port = srs_utest_random_port();
    listener->set_endpoint("127.0.0.1", port);
    srs_error_t err = listener->listen();
    if (err != srs_success) {
        srs_freep(err);
        return NULL;
    }

    srs_netfd_t stfd = NULL;
    if ((err = srs_tcp_connect("127.0.0.1", port, 1 * SRS_UTIME_SECONDS, &stfd)) != srs_success) {
        srs_freep(err);
        return NULL;
    }
    for (int i = 0; i < 100 && !acceptor->accepted_; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    return new SrsTcpConnection(stfd);
}

// Ends the client's side, then waits for the server's connections to end and leave the manager.
static void ai36_close_clients(SrsResourceManager *manager, vector<SrsTcpConnection *> &clients)
{
    for (int i = 0; i < (int)clients.size(); i++) {
        SrsTcpConnection *client = clients.at(i);
        srs_freep(client);
    }
    clients.clear();

    for (int i = 0; i < 300 && !manager->empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
}

// The server makes the connection of the listener a client comes from, and manages it until the client leaves.
VOID TEST(ServerTcpClientTest, EachListenerMakesItsConnection)
{
    srs_error_t err;

    SrsResourceManager manager("utest-ai36-listeners");
    manager.assemble();
    HELPER_ASSERT_SUCCESS(manager.start());

    MockStatisticForResampleKbps stat;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->conn_manager_ = &manager;

    ISrsListener *managed[] = {server->rtmp_listener_, server->rtmps_listener_, server->api_listener_, server->apis_listener_,
                               server->http_listener_, server->https_listener_, server->exporter_listener_,
#ifdef SRS_RTSP
                               server->rtsp_listener_
#endif
    };
    int nn = (int)(sizeof(managed) / sizeof(managed[0]));

    vector<MockServerTcpAcceptor *> acceptors;
    vector<SrsTcpListener *> listeners;
    vector<SrsTcpConnection *> clients;
    for (int i = 0; i < nn; i++) {
        MockServerTcpAcceptor *acceptor = new MockServerTcpAcceptor(server.get(), managed[i]);
        SrsTcpListener *listener = new SrsTcpListener(acceptor);
        acceptors.push_back(acceptor);
        listeners.push_back(listener);

        SrsTcpConnection *client = ai36_accept_client(acceptor, listener);
        ASSERT_TRUE(client != NULL);
        clients.push_back(client);

        EXPECT_TRUE(acceptor->accepted_) << i;
        EXPECT_STREQ("", acceptor->error_.c_str()) << i;
    }

    // Each client has its connection in the manager, whose traffic the server samples.
    EXPECT_EQ(nn, (int)manager.size());

    // An RTMP connection removes itself from the global manager, which is the server's in SRS, so point it to the
    // test's manager.
    for (int i = 0; i < (int)manager.size(); i++) {
        SrsRtmpConn *rtmp = dynamic_cast<SrsRtmpConn *>(manager.at(i));
        if (rtmp) {
            rtmp->manager_ = &manager;
        }
    }
    server->stat_ = &stat;
    server->resample_kbps();
    EXPECT_EQ(nn, stat.kbps_add_delta_count_);

    ai36_close_clients(&manager, clients);
    EXPECT_TRUE(manager.empty());

    for (int i = 0; i < nn; i++) {
        srs_freep(listeners[i]);
        srs_freep(acceptors[i]);
    }

    server->conn_manager_ = NULL;
    server->stat_ = NULL;
}

// A WebRTC over TCP client runs in an executor of its own, not in the manager.
VOID TEST(ServerTcpClientTest, WebrtcListenerRunsConnectionInExecutor)
{
    srs_error_t err;

    SrsResourceManager manager("utest-ai36-webrtc");
    manager.assemble();
    HELPER_ASSERT_SUCCESS(manager.start());

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->conn_manager_ = &manager;

    MockServerTcpAcceptor acceptor(server.get(), server->webrtc_listener_);
    SrsUniquePtr<SrsTcpListener> listener(new SrsTcpListener(&acceptor));
    vector<SrsTcpConnection *> clients;
    clients.push_back(ai36_accept_client(&acceptor, listener.get()));
    ASSERT_TRUE(clients[0] != NULL);

    EXPECT_TRUE(acceptor.accepted_);
    EXPECT_STREQ("", acceptor.error_.c_str());
    EXPECT_EQ(0, (int)manager.size());

    ai36_close_clients(&manager, clients);
    // Let the executor see the client is gone and free its connection.
    srs_usleep(100 * SRS_UTIME_MILLISECONDS);

    server->conn_manager_ = NULL;
}

// A client from a listener that is none of the server's is closed and not managed.
VOID TEST(ServerTcpClientTest, UnknownListenerClosesClient)
{
    srs_error_t err;

    MockConnectionManagerForResampleKbps manager;
    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->conn_manager_ = &manager;

    MockServerTcpAcceptor acceptor(server.get(), NULL);
    SrsUniquePtr<SrsTcpListener> listener(new SrsTcpListener(&acceptor));
    acceptor.as_ = listener.get();
    SrsUniquePtr<SrsTcpConnection> client(ai36_accept_client(&acceptor, listener.get()));
    ASSERT_TRUE(client.get() != NULL);

    EXPECT_TRUE(acceptor.accepted_);
    EXPECT_STREQ("", acceptor.error_.c_str());
    EXPECT_TRUE(manager.connections_.empty());

    // The server closed its end.
    char buf[16];
    ssize_t nread = 0;
    client->set_recv_timeout(1 * SRS_UTIME_SECONDS);
    err = client->read(buf, sizeof(buf), &nread);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);

    server->conn_manager_ = NULL;
}

// Over the connection limit, the server refuses a client before it makes any connection.
VOID TEST(ServerTcpClientTest, RefusesClientOverConnectionLimit)
{
    MockAppConfigForConnectionLimit config;
    config.max_connections_ = 1;
    MockConnectionManagerForConnectionLimit manager;
    manager.connection_count_ = 1;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->config_ = &config;
    server->conn_manager_ = &manager;

    MockServerTcpAcceptor acceptor(server.get(), server->rtmp_listener_);
    SrsUniquePtr<SrsTcpListener> listener(new SrsTcpListener(&acceptor));
    SrsUniquePtr<SrsTcpConnection> client(ai36_accept_client(&acceptor, listener.get()));
    ASSERT_TRUE(client.get() != NULL);

    EXPECT_TRUE(acceptor.accepted_);
    EXPECT_TRUE(acceptor.error_.find("drop TCP") != string::npos) << acceptor.error_;

    server->config_ = NULL;
    server->conn_manager_ = NULL;
}

#ifndef _WIN32 // POSIX only: int socket fd
// A client with no peer address, such as a load balancer's health check, is ignored when the config allows it.
// An unconnected socket has no peer on every OS, while glibc names the peer of an AF_UNIX pair "localhost".
VOID TEST(ServerTcpClientTest, EmptyPeerIpIsIgnoredOrRefused)
{
    srs_error_t err;

    MockAppConfigForEmptyIp config;
    MockConnectionManagerForResampleKbps manager;
    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->config_ = &config;
    server->conn_manager_ = &manager;

    if (true) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(fd, 0);
        srs_netfd_t stfd = srs_netfd_open_socket(fd);
        config.empty_ip_ok_ = true;
        HELPER_EXPECT_SUCCESS(server->on_tcp_client(server->rtmp_listener_, stfd));
        EXPECT_TRUE(manager.connections_.empty());
    }

    if (true) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(fd, 0);
        srs_netfd_t stfd = srs_netfd_open_socket(fd);
        config.empty_ip_ok_ = false;
        err = server->on_tcp_client(server->rtmp_listener_, stfd);
        EXPECT_EQ(ERROR_SOCKET_GET_PEER_IP, srs_error_code(err));
        srs_freep(err);
        EXPECT_TRUE(manager.connections_.empty());
    }

    server->config_ = NULL;
    server->conn_manager_ = NULL;
}
#endif

VOID TEST(ServerSrtClientTest, RefusesSocketWithoutPeer)
{
    srs_error_t err;

    MockConnectionManagerForResampleKbps manager;
    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->conn_manager_ = &manager;

    srs_srt_t fd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&fd));
    err = server->accept_srt_client(fd);
    EXPECT_TRUE(err != srs_success);
    EXPECT_TRUE(srs_error_desc(err).find("srt fd to resource") != string::npos);
    srs_freep(err);
    EXPECT_TRUE(manager.connections_.empty());

    server->conn_manager_ = NULL;
}

// An SRT listener and a client connected to it, freed with the pair.
class Ai36SrtPair
{
public:
    SrsSrtSocket *listener_;
    SrsSrtSocket *client_;

public:
    Ai36SrtPair()
    {
        listener_ = NULL;
        client_ = NULL;
    }
    ~Ai36SrtPair()
    {
        srs_freep(client_);
        srs_freep(listener_);
    }
};

// Connects an SRT client to a new listener, and returns the listener's end of the connection.
static srs_error_t ai36_srt_pair(Ai36SrtPair *pair, srs_srt_t *paccepted)
{
    srs_error_t err = srs_success;

    int port = 0;
    if ((err = srs_utest_free_port(SOCK_DGRAM, &port)) != srs_success) {
        return err;
    }

    srs_srt_t lfd = srs_srt_socket_invalid();
    if ((err = srs_srt_socket_with_default_option(&lfd)) != srs_success) {
        return err;
    }
    if ((err = srs_srt_listen(lfd, "127.0.0.1", port)) != srs_success) {
        srs_srt_close(lfd);
        return err;
    }
    pair->listener_ = new SrsSrtSocket(_srs_srt_eventloop->poller(), lfd);

    srs_srt_t cfd = srs_srt_socket_invalid();
    if ((err = srs_srt_socket_with_default_option(&cfd)) != srs_success) {
        return err;
    }
    pair->client_ = new SrsSrtSocket(_srs_srt_eventloop->poller(), cfd);
    pair->client_->set_send_timeout(3 * SRS_UTIME_SECONDS);
    if ((err = pair->client_->connect("127.0.0.1", port)) != srs_success) {
        return err;
    }

    pair->listener_->set_recv_timeout(3 * SRS_UTIME_SECONDS);
    return pair->listener_->accept(paccepted);
}

VOID TEST(ServerSrtClientTest, RefusesClientOverConnectionLimit)
{
    srs_error_t err;

    MockAppConfigForConnectionLimit config;
    config.max_connections_ = 1;
    MockConnectionManagerForConnectionLimit manager;
    manager.connection_count_ = 1;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->config_ = &config;
    server->conn_manager_ = &manager;

    Ai36SrtPair pair;
    srs_srt_t fd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(ai36_srt_pair(&pair, &fd));

    err = server->accept_srt_client(fd);
    EXPECT_EQ(ERROR_EXCEED_CONNECTIONS, srs_error_code(err));
    srs_freep(err);

    server->config_ = NULL;
    server->conn_manager_ = NULL;
}

VOID TEST(ServerSrtClientTest, ManagesConnectionOfClient)
{
    srs_error_t err;

    SrsResourceManager manager("utest-ai36-srt");
    manager.assemble();
    HELPER_ASSERT_SUCCESS(manager.start());

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->conn_manager_ = &manager;

    Ai36SrtPair pair;
    srs_srt_t fd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(ai36_srt_pair(&pair, &fd));

    HELPER_EXPECT_SUCCESS(server->accept_srt_client(fd));
    ASSERT_EQ(1, (int)manager.size());
    SrsMpegtsSrtConn *conn = dynamic_cast<SrsMpegtsSrtConn *>(manager.at(0));
    ASSERT_TRUE(conn != NULL);
    EXPECT_STREQ("127.0.0.1", conn->ip_.c_str());

    // SRT is off in the global config, so the connection refuses the client and the manager frees it.
    for (int i = 0; i < 300 && !manager.empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_TRUE(manager.empty());

    server->conn_manager_ = NULL;
}

MockLiveSourceForPublishMessage::MockLiveSourceForPublishMessage()
{
    on_aggregate_count_ = 0;
    on_meta_data_count_ = 0;
    on_edge_proxy_publish_count_ = 0;
}

MockLiveSourceForPublishMessage::~MockLiveSourceForPublishMessage()
{
}

srs_error_t MockLiveSourceForPublishMessage::on_aggregate(SrsRtmpCommonMessage *msg)
{
    on_aggregate_count_++;
    return srs_success;
}

srs_error_t MockLiveSourceForPublishMessage::on_meta_data(SrsRtmpCommonMessage *msg, SrsOnMetaDataPacket *metadata)
{
    on_meta_data_count_++;
    return srs_success;
}

srs_error_t MockLiveSourceForPublishMessage::on_edge_proxy_publish(SrsRtmpCommonMessage *msg)
{
    on_edge_proxy_publish_count_++;
    return srs_success;
}

MockAppConfigForRtmpConnCluster::MockAppConfigForRtmpConnCluster()
{
    tcp_nodelay_ = false;
}

MockAppConfigForRtmpConnCluster::~MockAppConfigForRtmpConnCluster()
{
}

std::vector<std::string> MockAppConfigForRtmpConnCluster::get_vhost_coworkers(std::string vhost)
{
    return coworkers_;
}

bool MockAppConfigForRtmpConnCluster::get_tcp_nodelay(std::string vhost)
{
    return tcp_nodelay_;
}

MockHttpHooksForCoworkers::MockHttpHooksForCoworkers()
{
}

MockHttpHooksForCoworkers::~MockHttpHooksForCoworkers()
{
    for (int i = 0; i < (int)errors_.size(); i++) {
        srs_error_t err = errors_.at(i);
        srs_freep(err);
    }
}

srs_error_t MockHttpHooksForCoworkers::discover_co_workers(std::string url, std::string &host, int &port)
{
    int i = (int)urls_.size();
    urls_.push_back(url);

    if (i < (int)errors_.size() && errors_.at(i) != srs_success) {
        return srs_error_copy(errors_.at(i));
    }
    if (i < (int)hosts_.size()) {
        host = hosts_.at(i);
        port = ports_.at(i);
    }
    return srs_success;
}

MockRtmpServerForRedirect::MockRtmpServerForRedirect()
{
    redirect_error_ = srs_success;
}

MockRtmpServerForRedirect::~MockRtmpServerForRedirect()
{
    srs_freep(redirect_error_);
}

srs_error_t MockRtmpServerForRedirect::redirect(ISrsRequest *r, std::string url, bool &accepted)
{
    redirect_urls_.push_back(url);
    return srs_error_copy(redirect_error_);
}

MockRtmpTransportForCycle::MockRtmpTransportForCycle()
{
    handshake_error_ = srs_success;
    set_tcp_nodelay_count_ = 0;
    tcp_nodelay_ = false;
    set_tcp_nodelay_error_ = srs_success;
}

MockRtmpTransportForCycle::~MockRtmpTransportForCycle()
{
    srs_freep(handshake_error_);
    srs_freep(set_tcp_nodelay_error_);
}

srs_error_t MockRtmpTransportForCycle::handshake()
{
    return srs_error_copy(handshake_error_);
}

srs_error_t MockRtmpTransportForCycle::set_tcp_nodelay(bool v)
{
    set_tcp_nodelay_count_++;
    tcp_nodelay_ = v;
    return srs_error_copy(set_tcp_nodelay_error_);
}

static SrsRtmpCommonMessage *ai36_rtmp_message(int type)
{
    SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
    msg->header_.message_type_ = type;
    msg->header_.payload_length_ = 10;
    msg->header_.timestamp_ = 1000;
    msg->header_.stream_id_ = 1;
    msg->create_payload(10);
    return msg;
}

// An edge proxies every message to the origin, and an origin feeds aggregates and metadata to the source.
VOID TEST(RtmpConnPublishMessageTest, EdgeProxiesAndOriginFeedsSource)
{
    srs_error_t err;

    MockAppConfigForRtmpConn config;
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(new MockRtmpTransportForDoCycle(), "192.168.1.100", 1935));
    conn->config_ = &config;
    conn->assemble();

    MockRtmpServer *rtmp = new MockRtmpServer();
    srs_freep(conn->rtmp_);
    conn->rtmp_ = rtmp;

    MockLiveSourceForPublishMessage *mock = new MockLiveSourceForPublishMessage();
    SrsSharedPtr<SrsLiveSource> source(mock);

    // An edge proxies even a video, which the origin would feed to the source.
    if (true) {
        conn->info_->edge_ = true;
        SrsUniquePtr<SrsRtmpCommonMessage> msg(ai36_rtmp_message(RTMP_MSG_VideoMessage));
        HELPER_EXPECT_SUCCESS(conn->process_publish_message(source, msg.get()));
        EXPECT_EQ(1, mock->on_edge_proxy_publish_count_);
        EXPECT_EQ(0, mock->on_video_count_);
        conn->info_->edge_ = false;
    }

    if (true) {
        SrsUniquePtr<SrsRtmpCommonMessage> msg(ai36_rtmp_message(RTMP_MSG_AggregateMessage));
        HELPER_EXPECT_SUCCESS(conn->process_publish_message(source, msg.get()));
        EXPECT_EQ(1, mock->on_aggregate_count_);
    }

    // The metadata goes to the source.
    if (true) {
        rtmp->decode_message_packet_ = new SrsOnMetaDataPacket();
        SrsUniquePtr<SrsRtmpCommonMessage> msg(ai36_rtmp_message(RTMP_MSG_AMF0DataMessage));
        HELPER_EXPECT_SUCCESS(conn->process_publish_message(source, msg.get()));
        EXPECT_EQ(1, rtmp->decode_message_count_);
        EXPECT_EQ(1, mock->on_meta_data_count_);
    }

    // Other data is ignored.
    if (true) {
        rtmp->decode_message_packet_ = new SrsCallPacket();
        SrsUniquePtr<SrsRtmpCommonMessage> msg(ai36_rtmp_message(RTMP_MSG_AMF3DataMessage));
        HELPER_EXPECT_SUCCESS(conn->process_publish_message(source, msg.get()));
        EXPECT_EQ(2, rtmp->decode_message_count_);
        EXPECT_EQ(1, mock->on_meta_data_count_);
    }

    // Data that does not decode fails the publish.
    if (true) {
        rtmp->decode_message_error_ = srs_error_new(ERROR_RTMP_AMF0_DECODE, "mock decode");
        SrsUniquePtr<SrsRtmpCommonMessage> msg(ai36_rtmp_message(RTMP_MSG_AMF0DataMessage));
        err = conn->process_publish_message(source, msg.get());
        EXPECT_EQ(ERROR_RTMP_AMF0_DECODE, srs_error_code(err));
        srs_freep(err);
        srs_freep(rtmp->decode_message_error_);
    }

    conn->config_ = NULL;
}

// A player's call is answered only when it has a transaction id, and its close stream closes the play.
VOID TEST(RtmpConnPlayControlTest, AnswersCallAndClosesOnCloseStream)
{
    srs_error_t err;

    MockAppConfigForRtmpConn config;
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(new MockRtmpTransportForDoCycle(), "192.168.1.100", 1935));
    conn->config_ = &config;
    conn->assemble();

    MockRtmpServer *rtmp = new MockRtmpServer();
    srs_freep(conn->rtmp_);
    conn->rtmp_ = rtmp;

    SrsSharedPtr<MockLiveSourceForQueue> source(new MockLiveSourceForQueue());
    SrsUniquePtr<MockLiveConsumerForPlayControl> consumer(new MockLiveConsumerForPlayControl(source.get()));

    if (true) {
        SrsCallPacket *call = new SrsCallPacket();
        call->transaction_id_ = 3;
        rtmp->decode_message_packet_ = call;
        HELPER_EXPECT_SUCCESS(conn->process_play_control_msg(consumer.get(), ai36_rtmp_message(RTMP_MSG_AMF0CommandMessage)));
        EXPECT_EQ(1, rtmp->send_and_free_packet_count_);
    }

    if (true) {
        SrsCallPacket *call = new SrsCallPacket();
        call->transaction_id_ = 0;
        rtmp->decode_message_packet_ = call;
        HELPER_EXPECT_SUCCESS(conn->process_play_control_msg(consumer.get(), ai36_rtmp_message(RTMP_MSG_AMF3CommandMessage)));
        EXPECT_EQ(1, rtmp->send_and_free_packet_count_);
    }

    if (true) {
        rtmp->decode_message_packet_ = new SrsCloseStreamPacket();
        err = conn->process_play_control_msg(consumer.get(), ai36_rtmp_message(RTMP_MSG_AMF0CommandMessage));
        EXPECT_EQ(ERROR_CONTROL_RTMP_CLOSE, srs_error_code(err));
        srs_freep(err);
    }

    conn->config_ = NULL;
}

// The socket's TCP_NODELAY follows the vhost, and is set only when it changes.
VOID TEST(RtmpConnSockOptionsTest, SetsTcpNodelayWhenChanged)
{
    MockAppConfigForRtmpConnCluster config;
    MockRtmpTransportForCycle *transport = new MockRtmpTransportForCycle();
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(transport, "192.168.1.100", 1935));
    conn->config_ = &config;
    conn->assemble();

    conn->set_sock_options();
    EXPECT_EQ(0, transport->set_tcp_nodelay_count_);

    config.tcp_nodelay_ = true;
    conn->set_sock_options();
    EXPECT_EQ(1, transport->set_tcp_nodelay_count_);
    EXPECT_TRUE(transport->tcp_nodelay_);
    EXPECT_TRUE(conn->tcp_nodelay_);

    conn->set_sock_options();
    EXPECT_EQ(1, transport->set_tcp_nodelay_count_);

    // A failure to set it is ignored, and the option is not set again.
    config.tcp_nodelay_ = false;
    transport->set_tcp_nodelay_error_ = srs_error_new(ERROR_SOCKET_SETKEEPALIVE, "mock nodelay");
    conn->set_sock_options();
    EXPECT_EQ(2, transport->set_tcp_nodelay_count_);
    EXPECT_FALSE(conn->tcp_nodelay_);
    conn->set_sock_options();
    EXPECT_EQ(2, transport->set_tcp_nodelay_count_);

    conn->config_ = NULL;
}

VOID TEST(RtmpConnClusterTest, RedirectsToCoworkerThatHasTheStream)
{
    srs_error_t err;

    MockAppConfigForRtmpConnCluster config;
    config.coworkers_.push_back("10.0.0.1:1985");
    config.coworkers_.push_back("10.0.0.2:1985");
    MockHttpHooksForCoworkers hooks;
    hooks.hosts_.push_back("");
    hooks.ports_.push_back(0);
    hooks.hosts_.push_back("10.0.0.2");
    hooks.ports_.push_back(19350);
    hooks.errors_.push_back(srs_error_new(ERROR_HTTP_DVR_NO_TAEGET, "mock not found"));
    hooks.errors_.push_back(srs_success);

    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(new MockRtmpTransportForDoCycle(), "192.168.1.100", 1935));
    conn->config_ = &config;
    conn->assemble();
    conn->hooks_ = &hooks;
    MockRtmpServerForRedirect *rtmp = new MockRtmpServerForRedirect();
    srs_freep(conn->rtmp_);
    conn->rtmp_ = rtmp;

    ISrsRequest *req = conn->info_->req_;
    req->vhost_ = "__defaultVhost__";
    req->host_ = "127.0.0.1";
    req->app_ = "live";
    req->stream_ = "livestream";

    SrsSharedPtr<SrsLiveSource> source(new MockLiveSource());
    err = conn->redirect_to_origin_cluster(source);
    EXPECT_EQ(ERROR_CONTROL_REDIRECT, srs_error_code(err));
    srs_freep(err);

    // The first coworker does not have the stream, so the second one is asked.
    ASSERT_EQ(2, (int)hooks.urls_.size());
    EXPECT_TRUE(hooks.urls_[0].find("http://10.0.0.1:1985/api/v1/clusters?") == 0) << hooks.urls_[0];
    EXPECT_TRUE(hooks.urls_[1].find("&stream=livestream&coworker=10.0.0.2:1985") != string::npos) << hooks.urls_[1];
    ASSERT_EQ(1, (int)rtmp->redirect_urls_.size());
    EXPECT_TRUE(rtmp->redirect_urls_[0].find("10.0.0.2:19350") != string::npos) << rtmp->redirect_urls_[0];

    conn->hooks_ = NULL;
    conn->config_ = NULL;
}

VOID TEST(RtmpConnClusterTest, FailsWhenNoCoworkerRedirects)
{
    srs_error_t err;

    // The last coworker fails to answer.
    if (true) {
        MockAppConfigForRtmpConnCluster config;
        config.coworkers_.push_back("10.0.0.1:1985");
        MockHttpHooksForCoworkers hooks;
        hooks.errors_.push_back(srs_error_new(ERROR_HTTP_DVR_NO_TAEGET, "mock not found"));

        SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(new MockRtmpTransportForDoCycle(), "192.168.1.100", 1935));
        conn->config_ = &config;
        conn->assemble();
        conn->hooks_ = &hooks;

        SrsSharedPtr<SrsLiveSource> source(new MockLiveSource());
        err = conn->redirect_to_origin_cluster(source);
        EXPECT_EQ(ERROR_HTTP_DVR_NO_TAEGET, srs_error_code(err));
        srs_freep(err);

        conn->hooks_ = NULL;
        conn->config_ = NULL;
    }

    // A coworker with no origin and a refused redirect leave no origin to go to.
    if (true) {
        MockAppConfigForRtmpConnCluster config;
        config.coworkers_.push_back("10.0.0.1:1985");
        config.coworkers_.push_back("10.0.0.2:1985");
        MockHttpHooksForCoworkers hooks;
        hooks.hosts_.push_back("10.0.0.3");
        hooks.ports_.push_back(0);
        hooks.hosts_.push_back("10.0.0.4");
        hooks.ports_.push_back(1935);

        SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(new MockRtmpTransportForDoCycle(), "192.168.1.100", 1935));
        conn->config_ = &config;
        conn->assemble();
        conn->hooks_ = &hooks;
        MockRtmpServerForRedirect *rtmp = new MockRtmpServerForRedirect();
        rtmp->redirect_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock redirect");
        srs_freep(conn->rtmp_);
        conn->rtmp_ = rtmp;

        SrsSharedPtr<SrsLiveSource> source(new MockLiveSource());
        err = conn->redirect_to_origin_cluster(source);
        EXPECT_EQ(ERROR_OCLUSTER_REDIRECT, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(2, (int)hooks.urls_.size());
        EXPECT_EQ(1, (int)rtmp->redirect_urls_.size());

        conn->hooks_ = NULL;
        conn->config_ = NULL;
    }
}

// The cycle logs how the client ended and never fails, and the connection leaves its manager.
VOID TEST(RtmpConnCycleTest, EndsQuietlyWhateverTheError)
{
    int codes[] = {ERROR_SUCCESS, ERROR_HTTP_STREAM_EOF, ERROR_RTMP_STREAM_NOT_FOUND};
    for (int i = 0; i < (int)(sizeof(codes) / sizeof(codes[0])); i++) {
        MockAppConfigForRtmpConn config;
        MockConnectionManagerForResampleKbps manager;
        MockStatisticForResampleKbps stat;
        MockRtmpTransportForCycle *transport = new MockRtmpTransportForCycle();
        transport->handshake_error_ = srs_error_new(codes[i], "mock handshake");

        SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(transport, "192.168.1.100", 1935));
        conn->config_ = &config;
        conn->assemble();
        conn->manager_ = &manager;
        conn->stat_ = &stat;

        srs_error_t err = conn->cycle();
        EXPECT_TRUE(err == srs_success) << codes[i];
        srs_freep(err);
        EXPECT_EQ(1, stat.kbps_add_delta_count_);

        conn->manager_ = NULL;
        conn->stat_ = NULL;
        conn->config_ = NULL;
    }
}

VOID TEST(RtmpConnTest, DescAndExpire)
{
    MockAppConfigForRtmpConn config;
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(new MockRtmpTransportForDoCycle(), "192.168.1.100", 1935));
    conn->config_ = &config;
    conn->assemble();

    MockCoroutineForRtmpConn *trd = new MockCoroutineForRtmpConn();
    srs_freep(conn->trd_);
    conn->trd_ = trd;

    EXPECT_STREQ("RtmpConn", conn->desc().c_str());
    EXPECT_STREQ("192.168.1.100", conn->remote_ip().c_str());
    conn->expire();
    EXPECT_EQ(1, trd->interrupt_count_);

    conn->config_ = NULL;
}

VOID TEST(RtmpTransportTest, RtmpsTransportUsesSsl)
{
    SrsRtmpsTransport transport(NULL);
    EXPECT_STREQ("ssl", transport.transport_type());
    EXPECT_TRUE(transport.io() == transport.ssl_);
    EXPECT_TRUE(transport.io() != NULL);
}

VOID TEST(RtmpTransportTest, SocketOptionsOfRealSocket)
{
    srs_error_t err;

    int port = srs_utest_random_port();
    MockTcpHandler handler;
    SrsTcpListener listener(&handler);
    listener.set_endpoint("127.0.0.1", port);
    HELPER_ASSERT_SUCCESS(listener.listen());

    srs_netfd_t stfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_tcp_connect("127.0.0.1", port, 1 * SRS_UTIME_SECONDS, &stfd));

    SrsRtmpTransport transport(stfd);
    EXPECT_EQ(srs_netfd_fileno(stfd), transport.osfd());
    HELPER_EXPECT_SUCCESS(transport.set_tcp_nodelay(true));
    HELPER_EXPECT_SUCCESS(transport.set_socket_buffer(350 * SRS_UTIME_MILLISECONDS));

    int v = 0;
    socklen_t size = sizeof(v);
    EXPECT_EQ(0, ::getsockopt(transport.osfd(), IPPROTO_TCP, TCP_NODELAY, (char *)&v, &size));
    EXPECT_NE(0, v);
}

// A play connection of the default vhost with mocks for its RTMP server, security and coroutine, which the
// connection owns.
class Ai36PlayConn
{
public:
    MockAppConfigForRtmpConn config_;
    SrsRtmpConn *conn_;
    MockRtmpServer *rtmp_;
    MockCoroutineForRtmpConn *trd_;

public:
    Ai36PlayConn()
    {
        conn_ = new SrsRtmpConn(new MockRtmpTransportForDoCycle(), "127.0.0.1", 1935);
        conn_->config_ = &config_;
        conn_->assemble();

        rtmp_ = new MockRtmpServer();
        srs_freep(conn_->rtmp_);
        conn_->rtmp_ = rtmp_;
        srs_freep(conn_->security_);
        conn_->security_ = new MockSecurity();
        trd_ = new MockCoroutineForRtmpConn();
        srs_freep(conn_->trd_);
        conn_->trd_ = trd_;

        rtmp_->type_ = SrsRtmpConnPlay;
        rtmp_->stream_ = "livestream";
        conn_->info_->req_->tcUrl_ = "rtmp://127.0.0.1/live";
        conn_->info_->req_->vhost_ = "__defaultVhost__";
        conn_->info_->req_->app_ = "live";
    }
    ~Ai36PlayConn()
    {
        srs_freep(conn_);
    }
};

// A republish or a close of the play is retried on the same connection, and any other control error ends it.
VOID TEST(RtmpConnServiceCycleTest, RetriesRepublishAndCloseOnly)
{
    srs_error_t err;

    int retried[] = {ERROR_CONTROL_REPUBLISH, ERROR_CONTROL_RTMP_CLOSE};
    for (int i = 0; i < 2; i++) {
        Ai36PlayConn c;
        c.rtmp_->start_play_error_ = srs_error_new(retried[i], "mock control");
        c.trd_->pull_error_ = srs_error_new(ERROR_THREAD_INTERRUPED, "mock quit");

        // The second pull of the loop, after the retry, ends the cycle.
        err = c.conn_->service_cycle();
        EXPECT_EQ(ERROR_THREAD_INTERRUPED, srs_error_code(err)) << retried[i];
        srs_freep(err);
        EXPECT_EQ(1, c.rtmp_->start_play_count_);
        EXPECT_EQ(2, c.trd_->pull_count_);
    }

    if (true) {
        Ai36PlayConn c;
        c.rtmp_->start_play_error_ = srs_error_new(ERROR_CONTROL_REDIRECT, "mock redirect");
        c.trd_->pull_error_ = srs_error_new(ERROR_THREAD_INTERRUPED, "mock quit");

        err = c.conn_->service_cycle();
        EXPECT_EQ(ERROR_CONTROL_REDIRECT, srs_error_code(err));
        EXPECT_TRUE(srs_error_desc(err).find("rtmp: reject") != string::npos);
        srs_freep(err);
        EXPECT_EQ(1, c.trd_->pull_count_);
    }
}

VOID TEST(RtmpConnServiceCycleTest, RefusesUnknownClientType)
{
    Ai36PlayConn c;
    c.rtmp_->type_ = SrsRtmpConnUnknown;

    srs_error_t err = c.conn_->stream_service_cycle();
    EXPECT_EQ(ERROR_SYSTEM_CLIENT_INVALID, srs_error_code(err));
    srs_freep(err);
}

// A client that puts its stream in the tcUrl, with no stream name, gets the stream from the app.
VOID TEST(RtmpConnServiceCycleTest, GuessesStreamFromTcUrl)
{
    Ai36PlayConn c;
    c.rtmp_->stream_ = "";
    c.conn_->info_->req_->tcUrl_ = "rtmp://127.0.0.1/live/livestream";
    c.rtmp_->start_play_error_ = srs_error_new(ERROR_RTMP_STREAM_NOT_FOUND, "mock play");

    srs_error_t err = c.conn_->stream_service_cycle();
    EXPECT_EQ(ERROR_RTMP_STREAM_NOT_FOUND, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(1, c.rtmp_->start_play_count_);
    EXPECT_STREQ("live", c.conn_->info_->req_->app_.c_str());
    EXPECT_STREQ("livestream", c.conn_->info_->req_->stream_.c_str());
}

// An edge SRS tells its identity in the connect args, which the origin reads before it serves the client.
VOID TEST(RtmpConnServiceCycleTest, ReadsEdgeIdentityFromConnectArgs)
{
    Ai36PlayConn c;
    c.rtmp_->set_request(SrsRtmpConnPlay, "127.0.0.1", "__defaultVhost__", "live", "livestream", "rtmp://127.0.0.1/live",
                         "rtmp", 1935, "127.0.0.1");

    SrsAmf0Object *args = SrsAmf0Any::object();
    args->set("srs_version", SrsAmf0Any::str("8.0.68"));
    args->set("srs_server_ip", SrsAmf0Any::str("10.0.0.1"));
    args->set("srs_pid", SrsAmf0Any::number(1234));
    args->set("srs_id", SrsAmf0Any::number(5));
    srs_freep(c.conn_->info_->req_->args_);
    c.conn_->info_->req_->args_ = args;

    // The service cycle starts, then its coroutine quits.
    c.trd_->pull_count_ = 1;
    c.trd_->pull_error_ = srs_error_new(ERROR_THREAD_INTERRUPED, "mock quit");
    srs_error_t err = c.conn_->do_cycle();
    EXPECT_EQ(ERROR_THREAD_INTERRUPED, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("service cycle") != string::npos);
    srs_freep(err);
    EXPECT_STREQ("127.0.0.1", c.conn_->info_->req_->ip_.c_str());
}

VOID TEST(HttpxConnTest, DescribesPlainAndSecureConnections)
{
    MockConnectionManagerForResampleKbps manager;

    if (true) {
        SrsHttpxConn conn(&manager, new SrsTcpConnection(NULL), NULL, "10.0.0.8", 8080, "", "");
        conn.assemble();
        EXPECT_STREQ("HttpConn", conn.desc().c_str());
        EXPECT_STREQ("10.0.0.8", conn.remote_ip().c_str());

        SrsHttpConn *hc = dynamic_cast<SrsHttpConn *>(conn.conn_);
        ASSERT_TRUE(hc != NULL);
        EXPECT_STREQ("HttpConn", hc->desc().c_str());
        EXPECT_TRUE(hc->delta() == hc->delta_);
        EXPECT_TRUE(hc->delta() != NULL);
        EXPECT_STREQ(hc->trd_->cid().c_str(), hc->get_id().c_str());
        EXPECT_TRUE(hc->handler() == &conn);
    }

    if (true) {
        SrsHttpxConn conn(&manager, new SrsTcpConnection(NULL), NULL, "10.0.0.8", 8088, "server.key", "server.crt");
        conn.assemble();
        EXPECT_STREQ("HttpsConn", conn.desc().c_str());
    }
}

VOID TEST(HttpxConnTest, ExpireInterruptsTheCoroutine)
{
    MockConnectionManagerForResampleKbps manager;
    SrsHttpxConn conn(&manager, new SrsTcpConnection(NULL), NULL, "10.0.0.8", 8080, "", "");
    conn.assemble();

    SrsHttpConn *hc = dynamic_cast<SrsHttpConn *>(conn.conn_);
    ASSERT_TRUE(hc != NULL);
    MockCoroutineForRtmpConn *trd = new MockCoroutineForRtmpConn();
    srs_freep(hc->trd_);
    hc->trd_ = trd;

    hc->expire();
    EXPECT_EQ(1, trd->interrupt_count_);
}

// The HLS session with no on_play or on_stop hook configured calls no hook, even when hooks are enabled.
VOID TEST(HlsStreamHooksTest, NoHookConfiguredCallsNoHook)
{
    srs_error_t err;

    MockSrsRequest req("__defaultVhost__", "live", "livestream");

    if (true) {
        MockAppConfigForHttpHooksOnPlay config;
        config.http_hooks_enabled_ = true;
        MockHttpHooksForOnPlay hooks;
        SrsHlsStream hls;
        hls.config_ = &config;
        hls.hooks_ = &hooks;
        HELPER_EXPECT_SUCCESS(hls.http_hooks_on_play(&req));
        EXPECT_EQ(0, hooks.on_play_count_);
        hls.config_ = NULL;
        hls.hooks_ = NULL;
    }

    if (true) {
        MockAppConfigForHttpHooksOnStop config;
        config.http_hooks_enabled_ = true;
        MockHttpHooks hooks;
        SrsHlsStream hls;
        hls.config_ = &config;
        hls.hooks_ = &hooks;
        hls.http_hooks_on_stop(&req);
        EXPECT_EQ(0, hooks.on_stop_count_);
        hls.config_ = NULL;
        hls.hooks_ = NULL;
    }
}

VOID TEST(HlsVirtualConnTest, ExpireStopsSessionAndRemovesClient)
{
    MockStatisticForResampleKbps stat;
    SrsHlsVirtualConn conn;
    conn.stat_ = &stat;
    conn.ctx_ = "ai36-ctx";

    conn.expire();
    EXPECT_TRUE(conn.interrupt_);

    conn.stat_ = NULL;
}

// A TS of an HLS session is served as a file, then its traffic goes to the session, not to the HTTP connection.
VOID TEST(VodStreamTsCtxTest, ServesTsAndStatsItForTheSession)
{
    srs_error_t err;

    MockConnectionManagerForResampleKbps manager;
    SrsHttpxConn httpx(&manager, new SrsTcpConnection(NULL), NULL, "10.0.0.8", 8080, "", "");
    httpx.assemble();
    httpx.set_enable_stat(true);
    SrsHttpConn *hc = dynamic_cast<SrsHttpConn *>(httpx.conn_);
    ASSERT_TRUE(hc != NULL);

    SrsHttpMuxEntry entry;
    entry.pattern = "/";
    SrsVodStream stream("/tmp");
    stream.set_fs_factory(new MockFileReaderFactoryForVodStream("0123456789"));
    stream.set_path(new MockSrsPathAlwaysExists());
    stream.entry_ = &entry;

    MockStatisticForResampleKbps stat;
    stream.hls_.stat_ = &stat;
    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    stream.hls_.alive("ai36-ctx", &req);

    MockResponseWriterForVodStream w;
    SrsHttpMessage r(NULL, NULL);
    HELPER_ASSERT_SUCCESS(r.set_url("/live/livestream-0.ts?hls_ctx=ai36-ctx", false));
    r.set_connection(hc);

    HELPER_EXPECT_SUCCESS(stream.serve_ts_ctx(&w, &r, "/tmp/live/livestream-0.ts"));
    EXPECT_FALSE(httpx.enable_stat_);
    EXPECT_EQ(1, stat.kbps_add_delta_count_);
    string resp = HELPER_BUFFER2STR(&w.io.out_buffer);
    EXPECT_TRUE(resp.find("0123456789") != string::npos) << resp;

    // A TS of an unknown session is served, but not stated.
    MockResponseWriterForVodStream w2;
    SrsHttpMessage r2(NULL, NULL);
    HELPER_ASSERT_SUCCESS(r2.set_url("/live/livestream-0.ts?hls_ctx=ai36-other", false));
    r2.set_connection(hc);
    HELPER_EXPECT_SUCCESS(stream.serve_ts_ctx(&w2, &r2, "/tmp/live/livestream-0.ts"));
    EXPECT_EQ(1, stat.kbps_add_delta_count_);

    stream.hls_.stat_ = NULL;
}

VOID TEST(HttpStaticServerTest, ServesThroughItsMux)
{
    srs_error_t err;

    SrsHttpStaticServer server;
    MockResponseWriter w;
    SrsHttpMessage r(NULL, NULL);
    HELPER_ASSERT_SUCCESS(r.set_url("/no/such/path", false));

    // Nothing is mounted, so the mux answers not found.
    HELPER_EXPECT_SUCCESS(server.serve_http(&w, &r));
    EXPECT_EQ(SRS_CONSTS_HTTP_NotFound, w.w->status_);
}

VOID TEST(FileLogTest, InitializeReadsGlobalConfig)
{
    srs_error_t err;

    SrsFileLog log;
    HELPER_EXPECT_SUCCESS(log.initialize());
    EXPECT_TRUE(log.config_ == (ISrsAppConfig *)_srs_config);
    EXPECT_EQ(_srs_config->get_log_tank_file(), log.log_to_file_tank_);
    EXPECT_EQ(_srs_config->get_utc_time(), log.utc_);
    EXPECT_EQ(srs_get_log_level(_srs_config->get_log_level()), log.level_);
}

class Ai36ReloadHandler : public ISrsReloadHandler
{
};

VOID TEST(ReloadHandlerTest, ChunkSizeIsIgnoredByDefault)
{
    srs_error_t err;

    Ai36ReloadHandler h;
    HELPER_EXPECT_SUCCESS(h.on_reload_vhost_chunk_size("__defaultVhost__"));
}

// Without a listen host or a coworker host, the service ip is the public address of the server.
VOID TEST(CoWorkersTest, DumpsPublicAddressWithoutAnyHost)
{
    srs_error_t err;

    MockAppConfigForCoWorkers config;
    config.listens_.push_back("19350");
    config.http_api_listens_.push_back("11985");
    config.default_vhost_ = new SrsConfDirective();
    config.default_vhost_->name_ = "vhost";
    config.default_vhost_->args_.push_back("__defaultVhost__");

    SrsCoWorkers *workers = new SrsCoWorkers();
    workers->config_ = &config;

    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    HELPER_EXPECT_SUCCESS(workers->on_publish(&req));

    SrsProtocolUtility utility;
    string ip = utility.public_internet_address();

    SrsUniquePtr<SrsJsonAny> data(workers->dumps("__defaultVhost__", "", "live", "livestream"));
    ASSERT_TRUE(data->is_object());
    EXPECT_STREQ(ip.c_str(), data->to_object()->get_property("ip")->to_str().c_str());
    EXPECT_STREQ((ip + ":11985").c_str(), data->to_object()->get_property("api")->to_str().c_str());

    // A stream that is not published is not found.
    SrsUniquePtr<SrsJsonAny> none(workers->dumps("__defaultVhost__", "", "live", "other"));
    EXPECT_TRUE(none->is_null());

    // The coworkers free the requests they keep.
    workers->config_ = NULL;
    srs_freep(workers);
}

VOID TEST(AppFactoryTest, FreedThroughItsInterface)
{
    ISrsAppFactory *factory = new SrsAppFactory();
    SrsUniquePtr<ISrsFileWriter> w(factory->create_file_writer());
    EXPECT_TRUE(w.get() != NULL);
    srs_freep(factory);
}

VOID TEST(UdpMuxSocketTest, SendonlyCopyKeepsPeerButNoBuffer)
{
    SrsUdpMuxSocket skt(NULL);
    skt.peer_ip_ = "10.0.0.9";
    skt.peer_port_ = 8000;
    skt.peer_id_ = "10.0.0.9:8000";
    skt.fast_id_ = 12345;
    skt.address_changed_ = true;
    skt.fromlen_ = 16;

    SrsUniquePtr<ISrsUdpMuxSocket> copied(skt.copy_sendonly());
    SrsUdpMuxSocket *c = dynamic_cast<SrsUdpMuxSocket *>(copied.get());
    ASSERT_TRUE(c != NULL);
    EXPECT_TRUE(c->buf_ == NULL);
    EXPECT_EQ(0, c->nb_buf_);
    EXPECT_EQ(0, c->nread_);
    EXPECT_STREQ("10.0.0.9", c->peer_ip_.c_str());
    EXPECT_EQ(8000, c->peer_port_);
    EXPECT_STREQ("10.0.0.9:8000", c->peer_id_.c_str());
    EXPECT_EQ(12345u, c->fast_id_);
    EXPECT_TRUE(c->address_changed_);
    EXPECT_EQ(16, c->fromlen_);
}

VOID TEST(AppUtilityTest, HostSelfProcStatIsTheGlobal)
{
    SrsHost host;
    EXPECT_TRUE(host.self_proc_stat() == srs_get_self_proc_stat());
}

VOID TEST(AppUtilityTest, PeerAndLocalAddressOfSockets)
{
    srs_error_t err;

    // An invalid socket has no address.
    EXPECT_STREQ("", srs_get_peer_ip(-1).c_str());
    EXPECT_EQ(0, srs_get_peer_port(-1));
    EXPECT_STREQ("", srs_get_local_ip(-1).c_str());
    EXPECT_EQ(0, srs_get_local_port(-1));

    // A socket bound to the IPv6 loopback has its port.
    srs_socket_t fd = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (fd != SRS_INVALID_SOCKET) {
        sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_loopback;
        if (::bind(fd, (sockaddr *)&addr, sizeof(addr)) == 0) {
            EXPECT_STREQ("::1", srs_get_local_ip((int)fd).c_str());
            EXPECT_GT(srs_get_local_port((int)fd), 0);
            // Not connected, so no peer.
            EXPECT_STREQ("", srs_get_peer_ip((int)fd).c_str());
        }
        srs_close_socket(fd);
    }

    // A connected IPv4 socket has its peer.
    int port = srs_utest_random_port();
    MockTcpHandler handler;
    SrsTcpListener listener(&handler);
    listener.set_endpoint("127.0.0.1", port);
    HELPER_ASSERT_SUCCESS(listener.listen());
    srs_netfd_t stfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_tcp_connect("127.0.0.1", port, 1 * SRS_UTIME_SECONDS, &stfd));
    SrsTcpConnection client(stfd);
    EXPECT_STREQ("127.0.0.1", srs_get_peer_ip(srs_netfd_fileno(stfd)).c_str());
    EXPECT_EQ(port, srs_get_peer_port(srs_netfd_fileno(stfd)));
}

#ifndef _WIN32 // POSIX only: fork and signals
VOID TEST(AppUtilityTest, KillStopsProcessBySigterm)
{
    srs_error_t err;

    SrsAppUtility utility;

    // No process to kill.
    int pid = 0;
    HELPER_EXPECT_SUCCESS(utility.kill(pid));
    EXPECT_EQ(0, pid);

    pid = srs_fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        for (;;) {
            ::sleep(1);
        }
    }

    HELPER_EXPECT_SUCCESS(utility.kill(pid));
    EXPECT_EQ(-1, pid);
}

VOID TEST(AppUtilityTest, KillFailsForProcessThatIsGone)
{
    SrsAppUtility utility;

    int pid = srs_fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        ::_exit(0);
    }
    int status = 0;
    ASSERT_EQ(pid, srs_waitpid(pid, &status, 0));

    int gone = pid;
    srs_error_t err = utility.kill(gone);
    EXPECT_EQ(ERROR_SYSTEM_KILL, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(pid, gone);
}
#endif

VOID TEST(PidFileLockerTest, FailsToOpenPidFileInMissingDirectory)
{
    MockAppConfigForPidFile config(_srs_tmp_file_prefix + "ai36-no-dir/srs.pid");
    SrsPidFileLocker locker;
    locker.config_ = &config;

    srs_error_t err = locker.acquire();
    EXPECT_EQ(ERROR_SYSTEM_PID_ACQUIRE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(-1, locker.pid_fd_);

    locker.config_ = NULL;
}

#ifndef _WIN32 // POSIX only: the death test forks
static void ai36_acquire_held_pid_file(ISrsAppConfig *config)
{
    SrsPidFileLocker locker;
    locker.config_ = config;
    srs_error_t err = locker.acquire();
    int code = srs_error_code(err);
    srs_freep(err);
    locker.config_ = NULL;
    exit(code == ERROR_SYSTEM_PID_ALREADY_RUNNING ? 0 : 1);
}

VOID TEST(PidFileLockerDeathTest, AnotherSrsFailsWhileItIsHeld)
{
    srs_error_t err;

    string path = _srs_tmp_file_prefix + "ai36-pid-held";
    MockFileRemover mfr(path);

    MockAppConfigForPidFile config(path);
    SrsPidFileLocker locker;
    locker.config_ = &config;
    HELPER_ASSERT_SUCCESS(locker.acquire());

    EXPECT_EXIT(ai36_acquire_held_pid_file(&config), ::testing::ExitedWithCode(0), "");

    locker.config_ = NULL;
}
#endif

// When WebRTC shares the HTTP port, a client that leaves before its first bytes is refused, with no connection made.
VOID TEST(ServerTcpClientTest, SharedPortRefusesClientThatSendsNothing)
{
    srs_error_t err;

    MockConnectionManagerForResampleKbps manager;
    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->conn_manager_ = &manager;
    server->reuse_rtc_over_server_ = true;

    MockServerTcpAcceptor acceptor(server.get(), server->http_listener_);
    SrsUniquePtr<SrsTcpListener> listener(new SrsTcpListener(&acceptor));
    int port = srs_utest_random_port();
    listener->set_endpoint("127.0.0.1", port);
    HELPER_ASSERT_SUCCESS(listener->listen());

    srs_netfd_t stfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_tcp_connect("127.0.0.1", port, 1 * SRS_UTIME_SECONDS, &stfd));
    srs_close_stfd(stfd);
    for (int i = 0; i < 100 && !acceptor.accepted_; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }

    ASSERT_TRUE(acceptor.accepted_);
    EXPECT_TRUE(acceptor.error_.find("peek") != string::npos) << acceptor.error_;
    EXPECT_TRUE(manager.connections_.empty());

    server->conn_manager_ = NULL;
}
