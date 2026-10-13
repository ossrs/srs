//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai17.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_coworkers.hpp>
#include <srs_app_dash.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_utest_ai06.hpp>
#include <srs_utest_ai13.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_ai23.hpp>
#include <srs_utest_ai32.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_fmp4.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_kernel.hpp>

#include <fstream>
#include <sstream>
#include <unistd.h>

// Mock ISrsMpdWriter implementation
MockMpdWriter::MockMpdWriter()
{
    file_home_ = "video";
    file_name_ = "segment-1.m4s";
    sequence_number_ = 1;
    get_fragment_called_ = false;
}

MockMpdWriter::~MockMpdWriter()
{
}

srs_error_t MockMpdWriter::get_fragment(bool video, std::string &home, std::string &filename, int64_t time, int64_t &sn)
{
    get_fragment_called_ = true;
    home = file_home_;
    filename = file_name_;
    sn = sequence_number_;
    return srs_success;
}

// Mock ISrsMp4M2tsSegmentEncoder implementation
MockMp4SegmentEncoder::MockMp4SegmentEncoder()
{
    initialize_called_ = false;
    write_sample_called_ = false;
    flush_called_ = false;
    last_sequence_ = 0;
    last_basetime_ = 0;
    last_tid_ = 0;
    last_handler_type_ = SrsMp4HandlerTypeForbidden;
    last_dts_ = 0;
    last_pts_ = 0;
    last_sample_size_ = 0;
}

MockMp4SegmentEncoder::~MockMp4SegmentEncoder()
{
}

srs_error_t MockMp4SegmentEncoder::initialize(ISrsWriter *w, uint32_t sequence, srs_utime_t basetime, uint32_t tid)
{
    initialize_called_ = true;
    last_sequence_ = sequence;
    last_basetime_ = basetime;
    last_tid_ = tid;
    return srs_success;
}

srs_error_t MockMp4SegmentEncoder::write_sample(SrsMp4HandlerType ht, uint16_t ft, uint32_t dts, uint32_t pts, uint8_t *sample, uint32_t nb_sample)
{
    write_sample_called_ = true;
    last_handler_type_ = ht;
    last_dts_ = dts;
    last_pts_ = pts;
    last_sample_size_ = nb_sample;
    return srs_success;
}

srs_error_t MockMp4SegmentEncoder::flush(uint64_t &dts)
{
    flush_called_ = true;
    dts = last_dts_;
    return srs_success;
}

// Mock ISrsFragment implementation
MockFragment::MockFragment()
{
    path_ = "";
    tmppath_ = "/tmp/test.mp4.tmp";
    number_ = 0;
    duration_ = 0;
    start_dts_ = 0;

    set_path_called_ = false;
    tmppath_called_ = false;
    rename_called_ = false;
    append_called_ = false;
    create_dir_called_ = false;
    set_number_called_ = false;
    number_called_ = false;
    duration_called_ = false;
    unlink_tmpfile_called_ = false;
    get_start_dts_called_ = false;
    unlink_file_called_ = false;

    append_dts_ = 0;
}

MockFragment::~MockFragment()
{
}

void MockFragment::set_path(std::string v)
{
    set_path_called_ = true;
    path_ = v;
}

std::string MockFragment::tmppath()
{
    tmppath_called_ = true;
    return tmppath_;
}

srs_error_t MockFragment::rename()
{
    rename_called_ = true;
    return srs_success;
}

void MockFragment::append(int64_t dts)
{
    append_called_ = true;
    append_dts_ = dts;
}

srs_error_t MockFragment::create_dir()
{
    create_dir_called_ = true;
    return srs_success;
}

void MockFragment::set_number(uint64_t n)
{
    set_number_called_ = true;
    number_ = n;
}

uint64_t MockFragment::number()
{
    number_called_ = true;
    return number_;
}

srs_utime_t MockFragment::duration()
{
    duration_called_ = true;
    return duration_;
}

srs_error_t MockFragment::unlink_tmpfile()
{
    unlink_tmpfile_called_ = true;
    return srs_success;
}

srs_utime_t MockFragment::get_start_dts()
{
    get_start_dts_called_ = true;
    return start_dts_;
}

srs_error_t MockFragment::unlink_file()
{
    unlink_file_called_ = true;
    return srs_success;
}

// Mock ISrsFragmentWindow implementation
MockFragmentWindow::MockFragmentWindow()
{
    dispose_called_ = false;
    append_called_ = false;
    shrink_called_ = false;
    clear_expired_called_ = false;
}

MockFragmentWindow::~MockFragmentWindow()
{
}

void MockFragmentWindow::dispose()
{
    dispose_called_ = true;
}

void MockFragmentWindow::append(ISrsFragment *fragment)
{
    append_called_ = true;
}

void MockFragmentWindow::shrink(srs_utime_t window)
{
    shrink_called_ = true;
}

void MockFragmentWindow::clear_expired(bool delete_files)
{
    clear_expired_called_ = true;
}

srs_utime_t MockFragmentWindow::max_duration()
{
    return 0;
}

bool MockFragmentWindow::empty()
{
    return true;
}

ISrsFragment *MockFragmentWindow::first()
{
    return NULL;
}

int MockFragmentWindow::size()
{
    return 0;
}

ISrsFragment *MockFragmentWindow::at(int index)
{
    return NULL;
}

// Mock ISrsFragmentedMp4 implementation
MockFragmentedMp4::MockFragmentedMp4()
{
    initialize_called_ = false;
    write_called_ = false;
    reap_called_ = false;
    unlink_tmpfile_called_ = false;
    unlink_tmpfile_error_ = srs_success;
    duration_ = 0;
    unlink_file_count_ = NULL;
}

MockFragmentedMp4::~MockFragmentedMp4()
{
}

srs_error_t MockFragmentedMp4::initialize(ISrsRequest *r, bool video, int64_t time, ISrsMpdWriter *mpd, uint32_t tid)
{
    initialize_called_ = true;
    return srs_success;
}

srs_error_t MockFragmentedMp4::write(SrsMediaPacket *shared_msg, SrsFormat *format)
{
    write_called_ = true;
    return srs_success;
}

srs_error_t MockFragmentedMp4::reap(uint64_t &dts)
{
    reap_called_ = true;
    return srs_success;
}

void MockFragmentedMp4::set_path(std::string v)
{
}

std::string MockFragmentedMp4::tmppath()
{
    return "";
}

srs_error_t MockFragmentedMp4::rename()
{
    return srs_success;
}

void MockFragmentedMp4::append(int64_t dts)
{
}

srs_error_t MockFragmentedMp4::create_dir()
{
    return srs_success;
}

void MockFragmentedMp4::set_number(uint64_t n)
{
}

uint64_t MockFragmentedMp4::number()
{
    return 0;
}

srs_utime_t MockFragmentedMp4::duration()
{
    return duration_;
}

srs_error_t MockFragmentedMp4::unlink_tmpfile()
{
    unlink_tmpfile_called_ = true;
    return srs_error_copy(unlink_tmpfile_error_);
}

srs_utime_t MockFragmentedMp4::get_start_dts()
{
    return 0;
}

srs_error_t MockFragmentedMp4::unlink_file()
{
    if (unlink_file_count_) {
        (*unlink_file_count_)++;
    }
    return srs_success;
}

// Mock ISrsInitMp4 implementation
MockInitMp4::MockInitMp4(MockDashAppFactory *factory)
{
    set_path_called_ = false;
    write_called_ = false;
    rename_called_ = false;
    path_ = "";
    video_ = false;
    tid_ = 0;
    factory_ = factory;
}

MockInitMp4::~MockInitMp4()
{
    // Copy state to factory before destruction so test can verify
    if (factory_) {
        factory_->last_set_path_called_ = set_path_called_;
        factory_->last_write_called_ = write_called_;
        factory_->last_rename_called_ = rename_called_;
        factory_->last_path_ = path_;
        factory_->last_video_ = video_;
        factory_->last_tid_ = tid_;
    }
    factory_ = NULL;
}

srs_error_t MockInitMp4::write(SrsFormat *format, bool video, int tid)
{
    write_called_ = true;
    video_ = video;
    tid_ = tid;
    return srs_success;
}

void MockInitMp4::set_path(std::string v)
{
    set_path_called_ = true;
    path_ = v;
}

std::string MockInitMp4::tmppath()
{
    return "";
}

srs_error_t MockInitMp4::rename()
{
    rename_called_ = true;
    return srs_success;
}

void MockInitMp4::append(int64_t dts)
{
}

srs_error_t MockInitMp4::create_dir()
{
    return srs_success;
}

void MockInitMp4::set_number(uint64_t n)
{
}

uint64_t MockInitMp4::number()
{
    return 0;
}

srs_utime_t MockInitMp4::duration()
{
    return 0;
}

srs_error_t MockInitMp4::unlink_tmpfile()
{
    return srs_success;
}

srs_utime_t MockInitMp4::get_start_dts()
{
    return 0;
}

srs_error_t MockInitMp4::unlink_file()
{
    return srs_success;
}

// Mock ISrsAppFactory implementation for DASH testing
MockDashAppFactory::MockDashAppFactory()
{
    last_set_path_called_ = false;
    last_write_called_ = false;
    last_rename_called_ = false;
    last_path_ = "";
    last_video_ = false;
    last_tid_ = 0;
}

MockDashAppFactory::~MockDashAppFactory()
{
}

ISrsInitMp4 *MockDashAppFactory::create_init_mp4()
{
    // Create a new mock init mp4 for testing
    // The caller takes ownership of this object
    // Pass 'this' so the mock can copy its state back before destruction
    MockInitMp4 *result = new MockInitMp4(this);
    return result;
}

// Mock ISrsDashController implementation
MockDashController::MockDashController()
{
    initialize_called_ = false;
    on_publish_called_ = false;
    on_unpublish_called_ = false;
    dispose_called_ = false;
    on_audio_count_ = 0;
    on_video_count_ = 0;
}

MockDashController::~MockDashController()
{
}

void MockDashController::dispose()
{
    dispose_called_ = true;
}

srs_error_t MockDashController::initialize(ISrsRequest *r)
{
    initialize_called_ = true;
    return srs_success;
}

srs_error_t MockDashController::on_publish()
{
    on_publish_called_ = true;
    return srs_success;
}

void MockDashController::on_unpublish()
{
    on_unpublish_called_ = true;
}

srs_error_t MockDashController::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    on_audio_count_++;
    return srs_success;
}

srs_error_t MockDashController::on_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    on_video_count_++;
    return srs_success;
}

// Declare the function to test
extern string srs_time_to_utc_format_str(srs_utime_t u);

VOID TEST(DashUtilityTest, TimeToUtcFormatStr)
{
    // Test Go's reference time: 2006-01-02 15:04:05 UTC
    // Unix timestamp: 1136214245 seconds
    srs_utime_t test_time = 1136214245 * SRS_UTIME_SECONDS;

    string result = srs_time_to_utc_format_str(test_time);

    // Expected format: "2006-01-02T15:04:05Z" (Go's standard time format)
    EXPECT_STREQ("2006-01-02T15:04:05Z", result.c_str());
}

VOID TEST(DashInitMp4Test, WriteVideoInit)
{
    srs_error_t err;

    // Create SrsInitMp4 object
    SrsUniquePtr<SrsInitMp4> init_mp4(new SrsInitMp4());

    // Create mock file writer
    SrsUniquePtr<MockSrsFileWriter> mock_fw(new MockSrsFileWriter());

    // Create mock format with video sequence header
    SrsUniquePtr<MockSrsFormat> format(new MockSrsFormat());

    // Set the path for the init mp4 file
    init_mp4->set_path("/tmp/dash_init_video.mp4");

    // Inject mock file writer
    srs_freep(init_mp4->fw_);
    init_mp4->fw_ = mock_fw.get();

    // Write video init mp4 with track id 1
    HELPER_EXPECT_SUCCESS(init_mp4->write(format.get(), true, 1));

    // Verify that file was written
    EXPECT_TRUE(mock_fw->filesize() > 0);

    // Verify the file contains expected MP4 boxes
    string content = mock_fw->str();
    EXPECT_TRUE(content.find("ftyp") != string::npos);
    EXPECT_TRUE(content.find("moov") != string::npos);

    // Clean up - set to NULL to avoid double-free
    init_mp4->fw_ = NULL;
}

VOID TEST(DashInitMp4Test, FragmentDelegation)
{
    srs_error_t err;

    // Create SrsInitMp4 object
    SrsUniquePtr<SrsInitMp4> init_mp4(new SrsInitMp4());

    // Create mock fragment
    MockFragment *mock_fragment = new MockFragment();

    // Inject mock fragment
    srs_freep(init_mp4->fragment_);
    init_mp4->fragment_ = mock_fragment;

    // Test set_path delegation
    init_mp4->set_path("/tmp/test_init.mp4");
    EXPECT_TRUE(mock_fragment->set_path_called_);
    EXPECT_STREQ("/tmp/test_init.mp4", mock_fragment->path_.c_str());

    // Test tmppath delegation
    string tmp = init_mp4->tmppath();
    EXPECT_TRUE(mock_fragment->tmppath_called_);
    EXPECT_STREQ("/tmp/test.mp4.tmp", tmp.c_str());

    // Test rename delegation
    HELPER_EXPECT_SUCCESS(init_mp4->rename());
    EXPECT_TRUE(mock_fragment->rename_called_);

    // Test append delegation
    init_mp4->append(12345);
    EXPECT_TRUE(mock_fragment->append_called_);
    EXPECT_EQ(12345, mock_fragment->append_dts_);

    // Test create_dir delegation
    HELPER_EXPECT_SUCCESS(init_mp4->create_dir());
    EXPECT_TRUE(mock_fragment->create_dir_called_);

    // Test set_number delegation
    init_mp4->set_number(100);
    EXPECT_TRUE(mock_fragment->set_number_called_);
    EXPECT_EQ(100, mock_fragment->number_);

    // Test number delegation
    uint64_t num = init_mp4->number();
    EXPECT_TRUE(mock_fragment->number_called_);
    EXPECT_EQ(100, num);

    // Test duration delegation
    mock_fragment->duration_ = 5 * SRS_UTIME_SECONDS;
    srs_utime_t dur = init_mp4->duration();
    EXPECT_TRUE(mock_fragment->duration_called_);
    EXPECT_EQ(5 * SRS_UTIME_SECONDS, dur);

    // Test unlink_tmpfile delegation
    HELPER_EXPECT_SUCCESS(init_mp4->unlink_tmpfile());
    EXPECT_TRUE(mock_fragment->unlink_tmpfile_called_);

    // Test get_start_dts delegation
    mock_fragment->start_dts_ = 67890 * SRS_UTIME_MILLISECONDS;
    srs_utime_t start_dts = init_mp4->get_start_dts();
    EXPECT_TRUE(mock_fragment->get_start_dts_called_);
    EXPECT_EQ(67890 * SRS_UTIME_MILLISECONDS, start_dts);

    // Test unlink_file delegation
    HELPER_EXPECT_SUCCESS(init_mp4->unlink_file());
    EXPECT_TRUE(mock_fragment->unlink_file_called_);

    // Clean up - set to NULL to avoid double-free
    init_mp4->fragment_ = NULL;
    srs_freep(mock_fragment);
}

VOID TEST(FragmentedMp4Test, InitializeWriteAndReap)
{
    srs_error_t err;

    // Create SrsFragmentedMp4 object
    SrsUniquePtr<SrsFragmentedMp4> fmp4(new SrsFragmentedMp4());

    // Create mock dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    MockMpdWriter *mock_mpd = new MockMpdWriter();
    MockMp4SegmentEncoder *mock_encoder = new MockMp4SegmentEncoder();
    MockSrsFileWriter *mock_fw = new MockSrsFileWriter();
    MockFragment *mock_fragment = new MockFragment();

    // Create mock request
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("__defaultVhost__", "live", "livestream"));

    // Inject mock dependencies into SrsFragmentedMp4
    fmp4->config_ = mock_config.get();
    srs_freep(fmp4->enc_);
    fmp4->enc_ = mock_encoder;
    srs_freep(fmp4->fw_);
    fmp4->fw_ = mock_fw;
    srs_freep(fmp4->fragment_);
    fmp4->fragment_ = mock_fragment;

    // Test initialize() - major use scenario step 1
    int64_t time = 1000000; // 1 second in microseconds
    uint32_t tid = 1;       // track id
    HELPER_EXPECT_SUCCESS(fmp4->initialize(mock_req.get(), true, time, mock_mpd, tid));

    // Verify initialize() called all dependencies correctly
    EXPECT_TRUE(mock_mpd->get_fragment_called_);
    EXPECT_TRUE(mock_fragment->set_path_called_);
    EXPECT_TRUE(mock_fragment->set_number_called_);
    EXPECT_EQ(1, mock_fragment->number_);
    EXPECT_TRUE(mock_fragment->create_dir_called_);
    EXPECT_TRUE(mock_fw->opened);
    EXPECT_TRUE(mock_encoder->initialize_called_);
    EXPECT_EQ(1, mock_encoder->last_sequence_);
    EXPECT_EQ(time, mock_encoder->last_basetime_);
    EXPECT_EQ(tid, mock_encoder->last_tid_);

    // Test write() with video packet - major use scenario step 2
    SrsUniquePtr<MockSrsMediaPacket> video_packet(new MockSrsMediaPacket(true, 1000));
    SrsUniquePtr<MockSrsFormat> format(new MockSrsFormat());

    // Set up video format with sample data
    uint8_t sample_data[10] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    format->raw_ = (char *)sample_data;
    format->nb_raw_ = 10;
    format->video_->frame_type_ = SrsVideoAvcFrameTypeKeyFrame;
    format->video_->cts_ = 40;

    HELPER_EXPECT_SUCCESS(fmp4->write(video_packet.get(), format.get()));

    // Verify write() called encoder correctly
    EXPECT_TRUE(mock_encoder->write_sample_called_);
    EXPECT_EQ(SrsMp4HandlerTypeVIDE, mock_encoder->last_handler_type_);
    EXPECT_EQ(1000, mock_encoder->last_dts_);
    EXPECT_EQ(1040, mock_encoder->last_pts_); // dts + cts
    EXPECT_EQ(10, mock_encoder->last_sample_size_);
    EXPECT_TRUE(mock_fragment->append_called_);
    EXPECT_EQ(1000, mock_fragment->append_dts_);

    // Test write() with audio packet
    mock_encoder->write_sample_called_ = false;
    mock_fragment->append_called_ = false;

    SrsUniquePtr<MockSrsMediaPacket> audio_packet(new MockSrsMediaPacket(false, 2000));
    uint8_t audio_data[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    format->raw_ = (char *)audio_data;
    format->nb_raw_ = 5;

    HELPER_EXPECT_SUCCESS(fmp4->write(audio_packet.get(), format.get()));

    // Verify audio write
    EXPECT_TRUE(mock_encoder->write_sample_called_);
    EXPECT_EQ(SrsMp4HandlerTypeSOUN, mock_encoder->last_handler_type_);
    EXPECT_EQ(2000, mock_encoder->last_dts_);
    EXPECT_EQ(2000, mock_encoder->last_pts_); // audio has no cts
    EXPECT_EQ(5, mock_encoder->last_sample_size_);
    EXPECT_TRUE(mock_fragment->append_called_);
    EXPECT_EQ(2000, mock_fragment->append_dts_);

    // Test reap() - major use scenario step 3
    uint64_t dts = 0;
    HELPER_EXPECT_SUCCESS(fmp4->reap(dts));

    // Verify reap() called flush and rename
    EXPECT_TRUE(mock_encoder->flush_called_);
    EXPECT_TRUE(mock_fragment->rename_called_);
    EXPECT_EQ(2000, dts); // Should return last dts from encoder

    // Clean up - set to NULL to avoid double-free
    // Note: fw_ was already freed by reap(), so just set to NULL
    fmp4->config_ = NULL;
    fmp4->fw_ = NULL;
    fmp4->enc_ = NULL;
    srs_freep(mock_encoder);
    fmp4->fragment_ = NULL;
    srs_freep(mock_fragment);
    srs_freep(mock_mpd);
}

VOID TEST(MpdWriterTest, OnPublish)
{
    srs_error_t err;

    // Create SrsMpdWriter object
    SrsUniquePtr<SrsMpdWriter> mpd_writer(new SrsMpdWriter());

    // Create mock config with DASH settings
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    // Inject mock config into SrsMpdWriter
    mpd_writer->config_ = mock_config.get();

    // Initialize the MPD writer with the request
    HELPER_EXPECT_SUCCESS(mpd_writer->initialize(mock_req.get()));

    // Call on_publish() - major use scenario
    HELPER_EXPECT_SUCCESS(mpd_writer->on_publish());

    // Verify that configuration values were loaded correctly
    // MockAppConfig returns default values:
    // - fragment: 30 seconds
    // - update_period: 30 seconds
    // - timeshift: 300 seconds
    // - path: "./[vhost]/[app]/[stream]/"
    // - mpd_file: "[stream].mpd"
    // - window_size: 10
    EXPECT_EQ(30 * SRS_UTIME_SECONDS, mpd_writer->fragment_);
    EXPECT_EQ(30 * SRS_UTIME_SECONDS, mpd_writer->update_period_);
    EXPECT_EQ(300 * SRS_UTIME_SECONDS, mpd_writer->timeshit_);
    EXPECT_STREQ("./[vhost]/[app]/[stream]/", mpd_writer->home_.c_str());
    EXPECT_STREQ("[stream].mpd", mpd_writer->mpd_file_.c_str());
    EXPECT_EQ(10, mpd_writer->window_size_);

    // Verify fragment_home_ was constructed correctly
    // mpd_file_ = "[stream].mpd" -> "livestream.mpd" (no slashes)
    // filepath_dir("livestream.mpd") returns "./" (no slash in path)
    // fragment_home_ = "./" + "/" + "livestream" = ".//livestream"
    EXPECT_STREQ(".//livestream", mpd_writer->fragment_home_.c_str());

    // Clean up - set to NULL to avoid double-free
    mpd_writer->config_ = NULL;
}

VOID TEST(MpdWriterTest, GetFragmentAndAvailabilityStartTime)
{
    srs_error_t err;

    // Create SrsMpdWriter object
    SrsUniquePtr<SrsMpdWriter> mpd_writer(new SrsMpdWriter());

    // Create mock config with DASH settings
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    // Inject mock config into SrsMpdWriter
    mpd_writer->config_ = mock_config.get();

    // Initialize the MPD writer with the request
    HELPER_EXPECT_SUCCESS(mpd_writer->initialize(mock_req.get()));

    // Call on_publish() to set up fragment duration
    HELPER_EXPECT_SUCCESS(mpd_writer->on_publish());

    // Test set_availability_start_time and get_availability_start_time
    srs_utime_t test_time = 1136214245 * SRS_UTIME_SECONDS; // 2006-01-02 15:04:05 UTC
    mpd_writer->set_availability_start_time(test_time);
    EXPECT_EQ(test_time, mpd_writer->get_availability_start_time());

    // Test get_fragment for video - major use scenario
    std::string video_home;
    std::string video_filename;
    int64_t video_time = 1000000; // 1 second in microseconds
    int64_t video_sn = 0;

    HELPER_EXPECT_SUCCESS(mpd_writer->get_fragment(true, video_home, video_filename, video_time, video_sn));

    // Verify video fragment results
    EXPECT_STREQ(".//livestream", video_home.c_str());
    EXPECT_STREQ("video-0.m4s", video_filename.c_str());
    EXPECT_EQ(0, video_sn);

    // Test get_fragment for audio - major use scenario
    std::string audio_home;
    std::string audio_filename;
    int64_t audio_time = 2000000; // 2 seconds in microseconds
    int64_t audio_sn = 0;

    HELPER_EXPECT_SUCCESS(mpd_writer->get_fragment(false, audio_home, audio_filename, audio_time, audio_sn));

    // Verify audio fragment results
    EXPECT_STREQ(".//livestream", audio_home.c_str());
    EXPECT_STREQ("audio-0.m4s", audio_filename.c_str());
    EXPECT_EQ(0, audio_sn);

    // Test that sequence numbers increment on subsequent calls
    HELPER_EXPECT_SUCCESS(mpd_writer->get_fragment(true, video_home, video_filename, video_time, video_sn));
    EXPECT_STREQ("video-1.m4s", video_filename.c_str());
    EXPECT_EQ(1, video_sn);

    HELPER_EXPECT_SUCCESS(mpd_writer->get_fragment(false, audio_home, audio_filename, audio_time, audio_sn));
    EXPECT_STREQ("audio-1.m4s", audio_filename.c_str());
    EXPECT_EQ(1, audio_sn);

    // Test that video and audio sequence numbers are independent
    HELPER_EXPECT_SUCCESS(mpd_writer->get_fragment(true, video_home, video_filename, video_time, video_sn));
    EXPECT_STREQ("video-2.m4s", video_filename.c_str());
    EXPECT_EQ(2, video_sn);

    HELPER_EXPECT_SUCCESS(mpd_writer->get_fragment(false, audio_home, audio_filename, audio_time, audio_sn));
    EXPECT_STREQ("audio-2.m4s", audio_filename.c_str());
    EXPECT_EQ(2, audio_sn);

    // Clean up - set to NULL to avoid double-free
    mpd_writer->config_ = NULL;
}

// Test: the MPD's durations keep their fractions of a second, from dash_update_period
// and the last segment's duration.
VOID TEST(MpdWriterTest, WriteDurations)
{
    srs_error_t err;

    SrsUniquePtr<SrsMpdWriter> mpd_writer(new SrsMpdWriter());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));
    mpd_writer->config_ = mock_config.get();

    HELPER_EXPECT_SUCCESS(mpd_writer->initialize(mock_req.get()));
    HELPER_EXPECT_SUCCESS(mpd_writer->on_publish());
    mpd_writer->home_ = "/tmp/srs-utest-dash-mpd";
    mpd_writer->update_period_ = 5500 * SRS_UTIME_MILLISECONDS;
    mpd_writer->window_size_ = 3;

    // One segment of 2.9s in each window.
    SrsFragmentWindow afragments, vfragments;
    SrsFragment *afragment = new SrsFragment();
    afragment->append(0);
    afragment->append(2900);
    afragments.append(afragment);
    SrsFragment *vfragment = new SrsFragment();
    vfragment->append(0);
    vfragment->append(2900);
    vfragments.append(vfragment);

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(mpd_writer->write(&format, &afragments, &vfragments));

    std::ifstream ifs("/tmp/srs-utest-dash-mpd/livestream.mpd");
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string mpd = ss.str();
    ::unlink("/tmp/srs-utest-dash-mpd/livestream.mpd");
    ::rmdir("/tmp/srs-utest-dash-mpd");

    EXPECT_TRUE(mpd.find("minimumUpdatePeriod=\"PT5.500S\"") != std::string::npos) << mpd;
    EXPECT_TRUE(mpd.find("timeShiftBufferDepth=\"PT8.700S\"") != std::string::npos) << mpd;
    EXPECT_TRUE(mpd.find("minBufferTime=\"PT5.800S\"") != std::string::npos) << mpd;

    mpd_writer->config_ = NULL;
}

// Write an MPD of one 2s segment in each window for the format, and return it.
static std::string mock_write_mpd(SrsFormat *format)
{
    srs_error_t err;

    SrsUniquePtr<SrsMpdWriter> mpd_writer(new SrsMpdWriter());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));
    mpd_writer->config_ = mock_config.get();

    HELPER_EXPECT_SUCCESS(mpd_writer->initialize(mock_req.get()));
    HELPER_EXPECT_SUCCESS(mpd_writer->on_publish());
    mpd_writer->home_ = "/tmp/srs-utest-dash-mpd";

    SrsFragmentWindow afragments, vfragments;
    SrsFragment *afragment = new SrsFragment();
    afragment->append(0);
    afragment->append(2000);
    afragments.append(afragment);
    SrsFragment *vfragment = new SrsFragment();
    vfragment->append(0);
    vfragment->append(2000);
    vfragments.append(vfragment);

    HELPER_EXPECT_SUCCESS(mpd_writer->write(format, &afragments, &vfragments));
    mpd_writer->config_ = NULL;

    std::ifstream ifs("/tmp/srs-utest-dash-mpd/livestream.mpd");
    std::stringstream ss;
    ss << ifs.rdbuf();
    ::unlink("/tmp/srs-utest-dash-mpd/livestream.mpd");
    ::rmdir("/tmp/srs-utest-dash-mpd");
    return ss.str();
}

// Test: the MPD's codecs come from the stream's codecs, as RFC 6381 and ISO/IEC 14496-15
// Annex E write them, not a fixed H.264 and AAC-LC.
VOID TEST(MpdWriterTest, WriteCodecs)
{
    // H.264 High 3.1 with the compatibility byte from the avcC, AAC-LC.
    if (true) {
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdAVC;
        uint8_t avcc[] = {0x01, 0x64, 0x00, 0x1f, 0xff};
        format.vcodec_->avc_extra_data_ = std::vector<char>((char *)avcc, (char *)avcc + sizeof(avcc));
        format.acodec_ = new SrsAudioCodecConfig();
        format.acodec_->id_ = SrsAudioCodecIdAAC;
        format.acodec_->aac_object_ = SrsAacObjectTypeAacLC;

        std::string mpd = mock_write_mpd(&format);
        EXPECT_TRUE(mpd.find("codecs=\"avc1.64001f\"") != std::string::npos) << mpd;
        EXPECT_TRUE(mpd.find("codecs=\"mp4a.40.2\"") != std::string::npos) << mpd;
    }

    // HEVC Main 3.1, main tier, the profile_compatibility_flags 0x60000000 in reverse
    // bit order, and the constraint bytes without the trailing zero bytes; HE-AAC.
    if (true) {
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdHEVC;
        SrsHevcDecoderConfigurationRecord *r = &format.vcodec_->hevc_dec_conf_record_;
        r->general_profile_space_ = 0;
        r->general_tier_flag_ = 0;
        r->general_profile_idc_ = 1;
        r->general_profile_compatibility_flags_ = 0x60000000;
        r->general_constraint_indicator_flags_ = 0x900000000000ULL;
        r->general_level_idc_ = 93;
        format.acodec_ = new SrsAudioCodecConfig();
        format.acodec_->id_ = SrsAudioCodecIdAAC;
        format.acodec_->aac_object_ = SrsAacObjectTypeAacHE;

        std::string mpd = mock_write_mpd(&format);
        EXPECT_TRUE(mpd.find("codecs=\"hev1.1.6.L93.90\"") != std::string::npos) << mpd;
        EXPECT_TRUE(mpd.find("codecs=\"mp4a.40.5\"") != std::string::npos) << mpd;
    }

    // HEVC Main 10 in high tier, profile space 1, two constraint bytes.
    if (true) {
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdHEVC;
        SrsHevcDecoderConfigurationRecord *r = &format.vcodec_->hevc_dec_conf_record_;
        r->general_profile_space_ = 1;
        r->general_tier_flag_ = 1;
        r->general_profile_idc_ = 2;
        r->general_profile_compatibility_flags_ = 0x20000000;
        r->general_constraint_indicator_flags_ = 0xb00800000000ULL;
        r->general_level_idc_ = 120;
        format.acodec_ = new SrsAudioCodecConfig();
        format.acodec_->id_ = SrsAudioCodecIdAAC;
        format.acodec_->aac_object_ = SrsAacObjectTypeAacLC;

        std::string mpd = mock_write_mpd(&format);
        EXPECT_TRUE(mpd.find("codecs=\"hev1.A2.4.H120.B0.8\"") != std::string::npos) << mpd;
    }

    // MP3 by the object type of its esds: MPEG-1 audio (0x6B) above 24kHz, MPEG-2 audio
    // (0x69) at 24kHz or below, not AAC.
    if (true) {
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdAVC;
        format.acodec_ = new SrsAudioCodecConfig();
        format.acodec_->id_ = SrsAudioCodecIdMP3;
        format.acodec_->sound_rate_ = SrsAudioSampleRate44100;

        std::string mpd = mock_write_mpd(&format);
        EXPECT_TRUE(mpd.find("codecs=\"mp4a.6B\"") != std::string::npos) << mpd;
    }
    if (true) {
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdAVC;
        format.acodec_ = new SrsAudioCodecConfig();
        format.acodec_->id_ = SrsAudioCodecIdMP3;
        format.acodec_->sound_rate_ = SrsAudioSampleRate22050;

        std::string mpd = mock_write_mpd(&format);
        EXPECT_TRUE(mpd.find("codecs=\"mp4a.69\"") != std::string::npos) << mpd;
    }
}

VOID TEST(DashControllerTest, InitializeAndDispose)
{
    srs_error_t err;

    // Create SrsDashController object
    SrsUniquePtr<SrsDashController> controller(new SrsDashController());

    // Create mock dependencies
    MockMpdWriter *mock_mpd = new MockMpdWriter();
    MockFragmentWindow *mock_vfragments = new MockFragmentWindow();
    MockFragmentWindow *mock_afragments = new MockFragmentWindow();
    MockFragmentedMp4 *mock_vcurrent = new MockFragmentedMp4();
    MockFragmentedMp4 *mock_acurrent = new MockFragmentedMp4();

    // Create mock request
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    // Inject mock dependencies into SrsDashController
    srs_freep(controller->mpd_);
    controller->mpd_ = mock_mpd;
    srs_freep(controller->vfragments_);
    controller->vfragments_ = mock_vfragments;
    srs_freep(controller->afragments_);
    controller->afragments_ = mock_afragments;
    controller->vcurrent_ = mock_vcurrent;
    controller->acurrent_ = mock_acurrent;

    // Test initialize() - major use scenario step 1
    HELPER_EXPECT_SUCCESS(controller->initialize(mock_req.get()));

    // Verify initialize() set the request correctly
    EXPECT_TRUE(controller->req_ != NULL);
    EXPECT_STREQ("test.vhost", controller->req_->vhost_.c_str());
    EXPECT_STREQ("live", controller->req_->app_.c_str());
    EXPECT_STREQ("livestream", controller->req_->stream_.c_str());

    // Test dispose() - major use scenario step 2
    controller->dispose();

    // Verify dispose() called all dependencies correctly
    EXPECT_TRUE(mock_vfragments->dispose_called_);
    EXPECT_TRUE(mock_afragments->dispose_called_);
    EXPECT_TRUE(mock_vcurrent->unlink_tmpfile_called_);
    EXPECT_TRUE(mock_acurrent->unlink_tmpfile_called_);

    // Clean up - set to NULL to avoid double-free
    controller->mpd_ = NULL;
    srs_freep(mock_mpd);
    controller->vfragments_ = NULL;
    srs_freep(mock_vfragments);
    controller->afragments_ = NULL;
    srs_freep(mock_afragments);
    controller->vcurrent_ = NULL;
    srs_freep(mock_vcurrent);
    controller->acurrent_ = NULL;
    srs_freep(mock_acurrent);
}

VOID TEST(DashControllerTest, OnPublishAndUnpublish)
{
    srs_error_t err;

    // Create SrsDashController object
    SrsUniquePtr<SrsDashController> controller(new SrsDashController());

    // Create mock dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    MockMpdWriter *mock_mpd = new MockMpdWriter();
    SrsUniquePtr<MockAppFactory> mock_factory(new MockAppFactory());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    // Initialize controller first
    HELPER_EXPECT_SUCCESS(controller->initialize(mock_req.get()));

    // Inject mock dependencies into SrsDashController
    controller->config_ = mock_config.get();
    srs_freep(controller->mpd_);
    controller->mpd_ = mock_mpd;
    controller->app_factory_ = mock_factory.get();

    // Test on_publish() - major use scenario
    HELPER_EXPECT_SUCCESS(controller->on_publish());

    // Verify on_publish() loaded configuration correctly
    // MockAppConfig returns default values:
    // - fragment: 30 seconds
    // - path: "./[vhost]/[app]/[stream]/"
    EXPECT_EQ(30 * SRS_UTIME_SECONDS, controller->fragment_);
    EXPECT_STREQ("./[vhost]/[app]/[stream]/", controller->home_.c_str());

    // Verify on_publish() created new fragment windows
    EXPECT_TRUE(controller->vfragments_ != NULL);
    EXPECT_TRUE(controller->afragments_ != NULL);

    // Verify on_publish() reset state variables
    EXPECT_EQ(0, controller->audio_dts_);
    EXPECT_EQ(0, controller->video_dts_);
    EXPECT_EQ(-1, controller->first_dts_);
    EXPECT_FALSE(controller->video_reaped_);

    // Create mock fragments for unpublish test, with a duration, so they are appended to the windows
    MockFragmentedMp4 *mock_vcurrent = new MockFragmentedMp4();
    MockFragmentedMp4 *mock_acurrent = new MockFragmentedMp4();
    mock_vcurrent->duration_ = 1 * SRS_UTIME_SECONDS;
    mock_acurrent->duration_ = 1 * SRS_UTIME_SECONDS;
    controller->vcurrent_ = mock_vcurrent;
    controller->acurrent_ = mock_acurrent;

    // Set some DTS values to test unpublish
    controller->video_dts_ = 1000;
    controller->audio_dts_ = 2000;

    // Test on_unpublish() - major use scenario
    controller->on_unpublish();

    // Verify on_unpublish() called reap on fragments
    EXPECT_TRUE(mock_vcurrent->reap_called_);
    EXPECT_TRUE(mock_acurrent->reap_called_);

    // Verify on_unpublish() appended fragments to windows, which own them now
    EXPECT_EQ(1, controller->vfragments_->size());
    EXPECT_EQ(1, controller->afragments_->size());
    EXPECT_TRUE(controller->vcurrent_ == NULL);
    EXPECT_TRUE(controller->acurrent_ == NULL);

    // Clean up - set to NULL to avoid double-free
    controller->config_ = NULL;
    controller->mpd_ = NULL;
    srs_freep(mock_mpd);
    controller->app_factory_ = NULL;
    // vcurrent_ and acurrent_ are already freed by on_unpublish or set to NULL
    controller->vcurrent_ = NULL;
    controller->acurrent_ = NULL;
}

// Test: a publish that ends right after a keyframe leaves a segment of that frame alone,
// with no duration, which the MPD never lists; the unpublish removes the file its reap
// renamed it to, so the dispose leaves no segment behind.
VOID TEST(DashControllerTest, UnpublishRemovesSegmentWithoutDuration)
{
    srs_error_t err;

    SrsUniquePtr<SrsDashController> controller(new SrsDashController());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    MockMpdWriter *mock_mpd = new MockMpdWriter();
    SrsUniquePtr<MockAppFactory> mock_factory(new MockAppFactory());
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    HELPER_EXPECT_SUCCESS(controller->initialize(mock_req.get()));
    controller->config_ = mock_config.get();
    srs_freep(controller->mpd_);
    controller->mpd_ = mock_mpd;
    controller->app_factory_ = mock_factory.get();
    HELPER_EXPECT_SUCCESS(controller->on_publish());

    // A video and an audio segment of one frame each.
    int unlinked = 0;
    MockFragmentedMp4 *mock_vcurrent = new MockFragmentedMp4();
    MockFragmentedMp4 *mock_acurrent = new MockFragmentedMp4();
    mock_vcurrent->unlink_file_count_ = &unlinked;
    mock_acurrent->unlink_file_count_ = &unlinked;
    controller->vcurrent_ = mock_vcurrent;
    controller->acurrent_ = mock_acurrent;

    controller->on_unpublish();

    // Both files removed, and not appended to the windows.
    EXPECT_EQ(2, unlinked);
    EXPECT_TRUE(controller->vcurrent_ == NULL);
    EXPECT_TRUE(controller->acurrent_ == NULL);
    EXPECT_EQ(0, controller->vfragments_->size());
    EXPECT_EQ(0, controller->afragments_->size());

    // A segment with a duration is appended to the window and kept.
    unlinked = 0;
    HELPER_EXPECT_SUCCESS(controller->on_publish());
    mock_vcurrent = new MockFragmentedMp4();
    mock_vcurrent->unlink_file_count_ = &unlinked;
    mock_vcurrent->duration_ = 1 * SRS_UTIME_SECONDS;
    controller->vcurrent_ = mock_vcurrent;

    controller->on_unpublish();

    EXPECT_EQ(0, unlinked);
    EXPECT_EQ(1, controller->vfragments_->size());

    controller->config_ = NULL;
    controller->mpd_ = NULL;
    srs_freep(mock_mpd);
    controller->app_factory_ = NULL;
    controller->vcurrent_ = NULL;
    controller->acurrent_ = NULL;
}

VOID TEST(DashControllerTest, OnAudioWritePacket)
{
    srs_error_t err;

    // Create SrsDashController object
    SrsUniquePtr<SrsDashController> controller(new SrsDashController());

    // Create mock dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    MockMpdWriter *mock_mpd = new MockMpdWriter();
    SrsUniquePtr<MockAppFactory> mock_factory(new MockAppFactory());
    MockFragmentedMp4 *mock_acurrent = new MockFragmentedMp4();
    MockFragmentWindow *mock_afragments = new MockFragmentWindow();

    // Create mock request
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    // Initialize controller
    HELPER_EXPECT_SUCCESS(controller->initialize(mock_req.get()));
    HELPER_EXPECT_SUCCESS(controller->on_publish());

    // Inject mock dependencies into SrsDashController
    controller->config_ = mock_config.get();
    srs_freep(controller->mpd_);
    controller->mpd_ = mock_mpd;
    controller->app_factory_ = mock_factory.get();
    controller->acurrent_ = mock_acurrent;
    srs_freep(controller->afragments_);
    controller->afragments_ = mock_afragments;

    // Create audio packet with timestamp 1000ms
    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->timestamp_ = 1000;
    audio_packet->message_type_ = SrsFrameTypeAudio;

    // Create format with audio codec (not sequence header)
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());

    // Simulate non-sequence-header audio packet by setting acodec but not sequence header
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->audio_ = new SrsParsedAudioPacket();
    format->audio_->aac_packet_type_ = SrsAudioAacFrameTraitRawData; // Not sequence header

    // Test on_audio() - major use scenario: write audio packet
    HELPER_EXPECT_SUCCESS(controller->on_audio(audio_packet.get(), format.get()));

    // Verify audio_dts_ was updated
    EXPECT_EQ(1000, controller->audio_dts_);

    // Verify first_dts_ was set
    EXPECT_EQ(1000, controller->first_dts_);

    // Verify acurrent_->write() was called
    EXPECT_TRUE(mock_acurrent->write_called_);

    // Verify afragments_->clear_expired() was called
    EXPECT_TRUE(mock_afragments->clear_expired_called_);

    // Clean up - set to NULL to avoid double-free
    controller->config_ = NULL;
    controller->mpd_ = NULL;
    srs_freep(mock_mpd);
    controller->app_factory_ = NULL;
    controller->acurrent_ = NULL;
    srs_freep(mock_acurrent);
    controller->afragments_ = NULL;
    srs_freep(mock_afragments);
}

// Test: MP3 has no sequence header, so its first frame, which stands for one, writes the
// audio init file the MPD points to, as the AAC sequence header does; it is not a sample.
// The next frames are written to the audio segment.
VOID TEST(DashControllerTest, OnAudioMp3WritesInit)
{
    srs_error_t err;

    SrsUniquePtr<SrsDashController> controller(new SrsDashController());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    MockMpdWriter *mock_mpd = new MockMpdWriter();
    SrsUniquePtr<MockDashAppFactory> mock_factory(new MockDashAppFactory());
    MockFragmentedMp4 *mock_acurrent = new MockFragmentedMp4();
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    HELPER_EXPECT_SUCCESS(controller->initialize(mock_req.get()));
    HELPER_EXPECT_SUCCESS(controller->on_publish());
    controller->config_ = mock_config.get();
    srs_freep(controller->mpd_);
    controller->mpd_ = mock_mpd;
    controller->app_factory_ = mock_factory.get();
    controller->acurrent_ = mock_acurrent;
    controller->home_ = "./dash";

    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdMP3;
    format->acodec_->sound_rate_ = SrsAudioSampleRate44100;
    format->audio_ = new SrsParsedAudioPacket();
    format->audio_->aac_packet_type_ = SrsAudioMp3FrameTraitSequenceHeader;

    // The first MP3 frame, a FLV audio tag of MP3 44.1kHz 16bit stereo.
    SrsUniquePtr<SrsMediaPacket> first(new SrsMediaPacket());
    first->timestamp_ = 0;
    first->message_type_ = SrsFrameTypeAudio;
    char *first_data = new char[5];
    first_data[0] = (char)0x2f;
    first_data[1] = (char)0xff;
    first_data[2] = (char)0xfb;
    first_data[3] = (char)0x90;
    first_data[4] = (char)0x64;
    first->wrap(first_data, 5);

    HELPER_EXPECT_SUCCESS(controller->on_audio(first.get(), format.get()));
    EXPECT_TRUE(mock_factory->last_write_called_);
    EXPECT_TRUE(mock_factory->last_rename_called_);
    EXPECT_FALSE(mock_factory->last_video_);
    EXPECT_EQ(2, mock_factory->last_tid_);
    EXPECT_TRUE(mock_factory->last_path_.find("/audio-init.mp4") != std::string::npos) << mock_factory->last_path_;
    EXPECT_STREQ(mock_factory->last_path_.c_str(), controller->audio_init_file_.c_str());
    EXPECT_FALSE(mock_acurrent->write_called_);

    // The next frame is a sample of the audio segment.
    mock_factory->last_write_called_ = false;
    format->audio_->aac_packet_type_ = SrsAudioMp3FrameTraitRawData;
    SrsUniquePtr<SrsMediaPacket> next(new SrsMediaPacket());
    next->timestamp_ = 26;
    next->message_type_ = SrsFrameTypeAudio;
    char *next_data = new char[5];
    memcpy(next_data, first_data, 5);
    next->wrap(next_data, 5);

    HELPER_EXPECT_SUCCESS(controller->on_audio(next.get(), format.get()));
    EXPECT_FALSE(mock_factory->last_write_called_);
    EXPECT_TRUE(mock_acurrent->write_called_);
    EXPECT_EQ(26, controller->audio_dts_);

    controller->config_ = NULL;
    controller->mpd_ = NULL;
    srs_freep(mock_mpd);
    controller->app_factory_ = NULL;
    controller->acurrent_ = NULL;
    srs_freep(mock_acurrent);
}

VOID TEST(DashControllerTest, OnVideoWriteAndReapFragment)
{
    srs_error_t err;

    // Create SrsDashController object
    SrsUniquePtr<SrsDashController> controller(new SrsDashController());

    // Create mock dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    MockMpdWriter *mock_mpd = new MockMpdWriter();
    SrsUniquePtr<MockAppFactory> mock_factory(new MockAppFactory());

    // Create mock fragments that will be returned by factory
    MockFragmentedMp4 *mock_vcurrent1 = new MockFragmentedMp4();
    MockFragmentedMp4 *mock_vcurrent2 = new MockFragmentedMp4();
    MockFragmentWindow *mock_vfragments = new MockFragmentWindow();

    // Create mock request
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    // Initialize controller
    HELPER_EXPECT_SUCCESS(controller->initialize(mock_req.get()));
    HELPER_EXPECT_SUCCESS(controller->on_publish());

    // Inject mock dependencies into SrsDashController
    controller->config_ = mock_config.get();
    srs_freep(controller->mpd_);
    controller->mpd_ = mock_mpd;
    controller->app_factory_ = mock_factory.get();

    // Inject first vcurrent fragment
    controller->vcurrent_ = mock_vcurrent1;
    srs_freep(controller->vfragments_);
    controller->vfragments_ = mock_vfragments;

    // Create first video packet (keyframe) with timestamp 1000ms
    SrsUniquePtr<SrsMediaPacket> video_packet1(new SrsMediaPacket());
    video_packet1->timestamp_ = 1000;
    video_packet1->message_type_ = SrsFrameTypeVideo;

    // Create format with video codec (not sequence header)
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());

    // Simulate non-sequence-header video packet (keyframe)
    format->vcodec_ = new SrsVideoCodecConfig();
    format->vcodec_->id_ = SrsVideoCodecIdAVC;
    format->video_ = new SrsParsedVideoPacket();
    format->video_->avc_packet_type_ = SrsVideoAvcFrameTraitNALU; // Not sequence header
    format->video_->frame_type_ = SrsVideoAvcFrameTypeKeyFrame;

    // Test on_video() - major use scenario step 1: write first video packet
    HELPER_EXPECT_SUCCESS(controller->on_video(video_packet1.get(), format.get()));

    // Verify video_dts_ was updated
    EXPECT_EQ(1000, controller->video_dts_);

    // Verify first_dts_ was set
    EXPECT_EQ(1000, controller->first_dts_);

    // Verify vcurrent_->write() was called
    EXPECT_TRUE(mock_vcurrent1->write_called_);

    // Verify vfragments_->clear_expired() was called
    EXPECT_TRUE(mock_vfragments->clear_expired_called_);

    // Reset flags for next test
    mock_vcurrent1->write_called_ = false;
    mock_vfragments->clear_expired_called_ = false;

    // Create second video packet (keyframe) with timestamp 31000ms (31 seconds)
    // This should trigger fragment reaping since default fragment duration is 30 seconds
    SrsUniquePtr<SrsMediaPacket> video_packet2(new SrsMediaPacket());
    video_packet2->timestamp_ = 31000;
    video_packet2->message_type_ = SrsFrameTypeVideo;

    // Set mock_vcurrent1 duration to exceed fragment threshold (30 seconds)
    mock_vcurrent1->duration_ = 31 * SRS_UTIME_SECONDS;

    // Prepare mock_factory to return mock_vcurrent2 when create_fragmented_mp4() is called
    mock_factory->real_fragmented_mp4_ = mock_vcurrent2;

    // Test on_video() - major use scenario step 2: reap fragment when duration exceeds threshold
    HELPER_EXPECT_SUCCESS(controller->on_video(video_packet2.get(), format.get()));

    // Verify video_dts_ was updated
    EXPECT_EQ(31000, controller->video_dts_);

    // Verify fragment was reaped
    EXPECT_TRUE(mock_vcurrent1->reap_called_);

    // Verify video_reaped_ flag was set
    EXPECT_TRUE(controller->video_reaped_);

    // Verify old fragment was appended to window
    EXPECT_TRUE(mock_vfragments->append_called_);

    // Verify new fragment was initialized
    EXPECT_TRUE(mock_vcurrent2->initialize_called_);

    // Verify new fragment received the write
    EXPECT_TRUE(mock_vcurrent2->write_called_);

    // Verify vfragments_->clear_expired() was called again
    EXPECT_TRUE(mock_vfragments->clear_expired_called_);

    // Clean up - set to NULL to avoid double-free
    controller->config_ = NULL;
    controller->mpd_ = NULL;
    srs_freep(mock_mpd);
    controller->app_factory_ = NULL;
    controller->vcurrent_ = NULL;
    srs_freep(mock_vcurrent2);
    controller->vfragments_ = NULL;
    srs_freep(mock_vfragments);
    // mock_vcurrent1 was already freed by controller when reaping
    srs_freep(mock_vcurrent1);
}

// Test: a video info frame, such as the colorInfo metadata of enhanced RTMP, carries no
// picture, so it is not written to the fragment and does not start the timeline.
VOID TEST(DashControllerTest, OnVideoSkipsInfoFrame)
{
    srs_error_t err;

    SrsUniquePtr<SrsDashController> controller(new SrsDashController());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    MockMpdWriter *mock_mpd = new MockMpdWriter();
    SrsUniquePtr<MockAppFactory> mock_factory(new MockAppFactory());
    MockFragmentedMp4 *mock_vcurrent = new MockFragmentedMp4();
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    HELPER_EXPECT_SUCCESS(controller->initialize(mock_req.get()));
    HELPER_EXPECT_SUCCESS(controller->on_publish());

    controller->config_ = mock_config.get();
    srs_freep(controller->mpd_);
    controller->mpd_ = mock_mpd;
    controller->app_factory_ = mock_factory.get();
    controller->vcurrent_ = mock_vcurrent;

    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());
    format->vcodec_ = new SrsVideoCodecConfig();
    format->vcodec_->id_ = SrsVideoCodecIdHEVC;
    format->video_ = new SrsParsedVideoPacket();

    // The info frame before the first picture is skipped.
    SrsUniquePtr<SrsMediaPacket> info(new SrsMediaPacket());
    info->timestamp_ = 1000;
    info->message_type_ = SrsFrameTypeVideo;
    format->video_->avc_packet_type_ = SrsVideoAvcFrameTraitNALU;
    format->video_->frame_type_ = SrsVideoAvcFrameTypeVideoInfoFrame;
    HELPER_EXPECT_SUCCESS(controller->on_video(info.get(), format.get()));
    EXPECT_FALSE(mock_vcurrent->write_called_);
    EXPECT_EQ(-1, controller->first_dts_);

    // The keyframe after it is written, and starts the timeline at its own time.
    SrsUniquePtr<SrsMediaPacket> keyframe(new SrsMediaPacket());
    keyframe->timestamp_ = 1040;
    keyframe->message_type_ = SrsFrameTypeVideo;
    format->video_->frame_type_ = SrsVideoAvcFrameTypeKeyFrame;
    HELPER_EXPECT_SUCCESS(controller->on_video(keyframe.get(), format.get()));
    EXPECT_TRUE(mock_vcurrent->write_called_);
    EXPECT_EQ(1040, controller->first_dts_);
    EXPECT_EQ(1040, controller->video_dts_);

    controller->config_ = NULL;
    controller->mpd_ = NULL;
    srs_freep(mock_mpd);
    controller->app_factory_ = NULL;
    controller->vcurrent_ = NULL;
    srs_freep(mock_vcurrent);
}

// The MPD in a dir per stream, so the dir of the MPD is not [app].
class MockAppConfigForDashMpdDir : public MockAppConfig
{
public:
    virtual std::string get_dash_mpd_file(std::string vhost) { return "[app]/[stream]/index.mpd"; }
};

// Test: the init files are written beside the segments, in the dir of the MPD's BaseURL,
// also when the MPD is not at [app]/[stream].mpd.
VOID TEST(DashControllerTest, InitBesideSegments)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfigForDashMpdDir> mock_config(new MockAppConfigForDashMpdDir());
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    // The dir of the segments, relative to dash_path.
    SrsUniquePtr<SrsMpdWriter> mpd(new SrsMpdWriter());
    mpd->config_ = mock_config.get();
    HELPER_EXPECT_SUCCESS(mpd->initialize(mock_req.get()));
    HELPER_EXPECT_SUCCESS(mpd->on_publish());
    std::string segment_home, file_name;
    int64_t sn = 0;
    HELPER_EXPECT_SUCCESS(mpd->get_fragment(true, segment_home, file_name, 0, sn));
    EXPECT_STREQ("live/livestream/livestream", segment_home.c_str());

    SrsUniquePtr<SrsDashController> controller(new SrsDashController());
    SrsUniquePtr<MockDashAppFactory> mock_factory(new MockDashAppFactory());
    HELPER_EXPECT_SUCCESS(controller->initialize(mock_req.get()));
    HELPER_EXPECT_SUCCESS(controller->on_publish());
    controller->config_ = mock_config.get();
    controller->app_factory_ = mock_factory.get();
    controller->home_ = "./dash";

    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());
    format->vcodec_ = new SrsVideoCodecConfig();
    format->vcodec_->id_ = SrsVideoCodecIdAVC;
    format->vcodec_->avc_extra_data_.push_back(0x01);
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->acodec_->aac_extra_data_.push_back(0x12);

    SrsUniquePtr<SrsMediaPacket> video_sh(new SrsMediaPacket());
    video_sh->message_type_ = SrsFrameTypeVideo;
    char *video_data = new char[5];
    memset(video_data, 0, 5);
    video_data[0] = 0x17;
    video_sh->wrap(video_data, 5);
    HELPER_EXPECT_SUCCESS(controller->refresh_init_mp4(video_sh.get(), format.get()));
    EXPECT_STREQ(("./dash/" + segment_home + "/video-init.mp4").c_str(), mock_factory->last_path_.c_str());

    SrsUniquePtr<SrsMediaPacket> audio_sh(new SrsMediaPacket());
    audio_sh->message_type_ = SrsFrameTypeAudio;
    char *audio_data = new char[4];
    audio_data[0] = (char)0xaf;
    audio_data[1] = 0x00;
    audio_data[2] = 0x12;
    audio_data[3] = 0x10;
    audio_sh->wrap(audio_data, 4);
    HELPER_EXPECT_SUCCESS(controller->refresh_init_mp4(audio_sh.get(), format.get()));
    EXPECT_STREQ(("./dash/" + segment_home + "/audio-init.mp4").c_str(), mock_factory->last_path_.c_str());

    controller->config_ = NULL;
    controller->app_factory_ = NULL;
    mpd->config_ = NULL;
}

// Test: the dispose removes the init files with the MPD and the segments, from the dir
// beside the segments where refresh_init_mp4 writes them.
VOID TEST(DashControllerTest, DisposeRemovesInitFiles)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfigForDashMpdDir> mock_config(new MockAppConfigForDashMpdDir());
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    SrsUniquePtr<SrsDashController> controller(new SrsDashController());
    SrsUniquePtr<MockDashAppFactory> mock_factory(new MockDashAppFactory());
    HELPER_EXPECT_SUCCESS(controller->initialize(mock_req.get()));
    controller->config_ = mock_config.get();
    controller->app_factory_ = mock_factory.get();
    controller->home_ = "/tmp/srs-utest-dash-dispose";

    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());
    format->vcodec_ = new SrsVideoCodecConfig();
    format->vcodec_->id_ = SrsVideoCodecIdAVC;
    format->vcodec_->avc_extra_data_.push_back(0x01);
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->acodec_->aac_extra_data_.push_back(0x12);

    SrsUniquePtr<SrsMediaPacket> video_sh(new SrsMediaPacket());
    video_sh->message_type_ = SrsFrameTypeVideo;
    char *video_data = new char[5];
    memset(video_data, 0, 5);
    video_data[0] = 0x17;
    video_sh->wrap(video_data, 5);

    SrsUniquePtr<SrsMediaPacket> audio_sh(new SrsMediaPacket());
    audio_sh->message_type_ = SrsFrameTypeAudio;
    char *audio_data = new char[4];
    audio_data[0] = (char)0xaf;
    audio_data[1] = 0x00;
    audio_data[2] = 0x12;
    audio_data[3] = 0x10;
    audio_sh->wrap(audio_data, 4);

    // The init files, where refresh_init_mp4 writes them for this MPD; the mock init mp4
    // writes nothing, so the test makes the files.
    SrsPath path;
    std::string dir = "/tmp/srs-utest-dash-dispose/live/livestream/livestream";
    HELPER_EXPECT_SUCCESS(controller->refresh_init_mp4(video_sh.get(), format.get()));
    HELPER_EXPECT_SUCCESS(controller->refresh_init_mp4(audio_sh.get(), format.get()));
    std::ofstream(std::string(dir + "/video-init.mp4").c_str()) << "init";
    std::ofstream(std::string(dir + "/audio-init.mp4").c_str()) << "init";
    EXPECT_TRUE(path.exists(dir + "/video-init.mp4"));
    EXPECT_TRUE(path.exists(dir + "/audio-init.mp4"));

    controller->dispose();
    bool video_left = path.exists(dir + "/video-init.mp4");
    bool audio_left = path.exists(dir + "/audio-init.mp4");

    // A stream with no audio has no audio init file; the dispose ignores the missing file.
    HELPER_EXPECT_SUCCESS(controller->refresh_init_mp4(video_sh.get(), format.get()));
    std::ofstream(std::string(dir + "/video-init.mp4").c_str()) << "init";
    controller->dispose();
    bool video_only_left = path.exists(dir + "/video-init.mp4");

    ::unlink(std::string(dir + "/video-init.mp4").c_str());
    ::unlink(std::string(dir + "/audio-init.mp4").c_str());
    ::rmdir("/tmp/srs-utest-dash-dispose/live/livestream/livestream");
    ::rmdir("/tmp/srs-utest-dash-dispose/live/livestream");
    ::rmdir("/tmp/srs-utest-dash-dispose/live");
    ::rmdir("/tmp/srs-utest-dash-dispose");
    controller->config_ = NULL;
    controller->app_factory_ = NULL;

    EXPECT_FALSE(video_left);
    EXPECT_FALSE(audio_left);
    EXPECT_FALSE(video_only_left);
}

// Test: the dispose of a stream that never published DASH removes no init file. The
// stream has no dash_path then, so its init dir would be relative to the root; the app
// here makes that root-relative dir /tmp/srs-utest-dash-root, where the test can look.
VOID TEST(DashControllerTest, DisposeNeverPublishedKeepsRootFiles)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfigForDashMpdDir> mock_config(new MockAppConfigForDashMpdDir());
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "tmp/srs-utest-dash-root", "livestream"));

    SrsUniquePtr<SrsDashController> controller(new SrsDashController());
    HELPER_EXPECT_SUCCESS(controller->initialize(mock_req.get()));
    controller->config_ = mock_config.get();

    SrsPath path;
    std::string dir = "/tmp/srs-utest-dash-root/livestream/livestream";
    HELPER_EXPECT_SUCCESS(path.mkdir_all(dir));
    std::ofstream(std::string(dir + "/video-init.mp4").c_str()) << "init";
    std::ofstream(std::string(dir + "/audio-init.mp4").c_str()) << "init";

    controller->dispose();
    bool video_kept = path.exists(dir + "/video-init.mp4");
    bool audio_kept = path.exists(dir + "/audio-init.mp4");

    ::unlink(std::string(dir + "/video-init.mp4").c_str());
    ::unlink(std::string(dir + "/audio-init.mp4").c_str());
    ::rmdir("/tmp/srs-utest-dash-root/livestream/livestream");
    ::rmdir("/tmp/srs-utest-dash-root/livestream");
    ::rmdir("/tmp/srs-utest-dash-root");
    controller->config_ = NULL;

    EXPECT_TRUE(video_kept);
    EXPECT_TRUE(audio_kept);
}

VOID TEST(DashControllerTest, RefreshInitMp4AndMpd)
{
    srs_error_t err;

    // Create SrsDashController object
    SrsUniquePtr<SrsDashController> controller(new SrsDashController());

    // Create mock dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    MockMpdWriter *mock_mpd = new MockMpdWriter();
    SrsUniquePtr<MockDashAppFactory> mock_factory(new MockDashAppFactory());
    MockFragmentWindow *mock_vfragments = new MockFragmentWindow();
    MockFragmentWindow *mock_afragments = new MockFragmentWindow();

    // Create mock request
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    // Initialize controller
    HELPER_EXPECT_SUCCESS(controller->initialize(mock_req.get()));
    HELPER_EXPECT_SUCCESS(controller->on_publish());

    // Inject mock dependencies into SrsDashController
    controller->config_ = mock_config.get();
    srs_freep(controller->mpd_);
    controller->mpd_ = mock_mpd;
    controller->app_factory_ = mock_factory.get();
    srs_freep(controller->vfragments_);
    controller->vfragments_ = mock_vfragments;
    srs_freep(controller->afragments_);
    controller->afragments_ = mock_afragments;

    // Set home directory for testing
    controller->home_ = "./dash";

    // Create video sequence header packet
    SrsUniquePtr<SrsMediaPacket> video_sh(new SrsMediaPacket());
    video_sh->timestamp_ = 0;
    video_sh->message_type_ = SrsFrameTypeVideo;
    char *video_data = new char[10];
    video_data[0] = 0x17;
    video_data[1] = 0x00;
    video_data[2] = 0x00;
    video_data[3] = 0x00;
    video_data[4] = 0x00;
    video_data[5] = 0x01;
    video_data[6] = 0x02;
    video_data[7] = 0x03;
    video_data[8] = 0x04;
    video_data[9] = 0x05;
    video_sh->wrap(video_data, 10);

    // Create format with video codec sequence header
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());
    format->vcodec_ = new SrsVideoCodecConfig();
    format->vcodec_->id_ = SrsVideoCodecIdAVC;
    format->video_ = new SrsParsedVideoPacket();
    format->video_->avc_packet_type_ = SrsVideoAvcFrameTraitSequenceHeader;
    format->video_->frame_type_ = SrsVideoAvcFrameTypeKeyFrame;
    // Set codec as ready - need avc_extra_data_ to be non-empty
    format->vcodec_->avc_extra_data_.push_back(0x01);
    format->vcodec_->avc_extra_data_.push_back(0x42);
    format->vcodec_->avc_extra_data_.push_back(0x00);

    // Create audio sequence header packet
    SrsUniquePtr<SrsMediaPacket> audio_sh(new SrsMediaPacket());
    audio_sh->timestamp_ = 0;
    audio_sh->message_type_ = SrsFrameTypeAudio;
    char *audio_data = new char[4];
    audio_data[0] = (char)0xaf;
    audio_data[1] = 0x00;
    audio_data[2] = 0x12;
    audio_data[3] = 0x10;
    audio_sh->wrap(audio_data, 4);

    // Set audio codec sequence header
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->audio_ = new SrsParsedAudioPacket();
    format->audio_->aac_packet_type_ = SrsAudioAacFrameTraitSequenceHeader;
    // Set codec as ready
    format->acodec_->aac_extra_data_.push_back(0x12);
    format->acodec_->aac_extra_data_.push_back(0x10);

    // Test refresh_init_mp4() for video - major use scenario
    HELPER_EXPECT_SUCCESS(controller->refresh_init_mp4(video_sh.get(), format.get()));

    // Verify video init mp4 was created correctly (state copied from mock before destruction)
    EXPECT_TRUE(mock_factory->last_set_path_called_);
    EXPECT_TRUE(mock_factory->last_write_called_);
    EXPECT_TRUE(mock_factory->last_rename_called_);
    EXPECT_TRUE(mock_factory->last_video_);
    EXPECT_EQ(1, mock_factory->last_tid_); // video_track_id_ is 1
    EXPECT_TRUE(mock_factory->last_path_.find("video-init.mp4") != std::string::npos);

    // Reset factory state for audio test
    mock_factory->last_set_path_called_ = false;
    mock_factory->last_write_called_ = false;
    mock_factory->last_rename_called_ = false;
    mock_factory->last_path_ = "";
    mock_factory->last_video_ = false;
    mock_factory->last_tid_ = 0;

    // Test refresh_init_mp4() for audio - major use scenario
    HELPER_EXPECT_SUCCESS(controller->refresh_init_mp4(audio_sh.get(), format.get()));

    // Verify audio init mp4 was created correctly (state copied from mock before destruction)
    EXPECT_TRUE(mock_factory->last_set_path_called_);
    EXPECT_TRUE(mock_factory->last_write_called_);
    EXPECT_TRUE(mock_factory->last_rename_called_);
    EXPECT_FALSE(mock_factory->last_video_);
    EXPECT_EQ(2, mock_factory->last_tid_); // audio_track_id_ is 2
    EXPECT_TRUE(mock_factory->last_path_.find("audio-init.mp4") != std::string::npos);

    // Test refresh_mpd() - major use scenario
    HELPER_EXPECT_SUCCESS(controller->refresh_mpd(format.get()));

    // Verify MPD write was called (no direct way to verify, but no error means success)
    // The method should call mpd_->write(format, afragments_, vfragments_)

    // Test refresh_mpd() with NULL format - should return success without error
    HELPER_EXPECT_SUCCESS(controller->refresh_mpd(NULL));

    // Test refresh_mpd() with missing audio codec - should return success without error
    SrsUniquePtr<SrsFormat> format_no_audio(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format_no_audio->initialize());
    format_no_audio->vcodec_ = new SrsVideoCodecConfig();
    format_no_audio->vcodec_->id_ = SrsVideoCodecIdAVC;
    HELPER_EXPECT_SUCCESS(controller->refresh_mpd(format_no_audio.get()));

    // Test refresh_mpd() with missing video codec - should return success without error
    SrsUniquePtr<SrsFormat> format_no_video(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format_no_video->initialize());
    format_no_video->acodec_ = new SrsAudioCodecConfig();
    format_no_video->acodec_->id_ = SrsAudioCodecIdAAC;
    HELPER_EXPECT_SUCCESS(controller->refresh_mpd(format_no_video.get()));

    // Test refresh_init_mp4() with empty packet - should return success without creating init mp4
    SrsUniquePtr<SrsMediaPacket> empty_packet(new SrsMediaPacket());
    empty_packet->timestamp_ = 0;
    empty_packet->message_type_ = SrsFrameTypeVideo;
    // Don't wrap any data, so size() will be 0
    HELPER_EXPECT_SUCCESS(controller->refresh_init_mp4(empty_packet.get(), format.get()));

    // Test refresh_init_mp4() with codec not ready - should return success without creating init mp4
    SrsUniquePtr<SrsFormat> format_not_ready(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format_not_ready->initialize());
    format_not_ready->vcodec_ = new SrsVideoCodecConfig();
    format_not_ready->vcodec_->id_ = SrsVideoCodecIdAVC;
    // Don't set SPS/PPS, so is_avc_codec_ok() will return false
    HELPER_EXPECT_SUCCESS(controller->refresh_init_mp4(video_sh.get(), format_not_ready.get()));

    // Clean up - set to NULL to avoid double-free
    controller->config_ = NULL;
    controller->mpd_ = NULL;
    srs_freep(mock_mpd);
    controller->app_factory_ = NULL;
    controller->vfragments_ = NULL;
    srs_freep(mock_vfragments);
    controller->afragments_ = NULL;
    srs_freep(mock_afragments);
}

// Test SrsDash lifecycle: initialize, dispose, and cleanup_delay
// This test covers the major use scenario for SrsDash lifecycle management
VOID TEST(DashTest, LifecycleInitializeDisposeCleanupDelay)
{
    srs_error_t err;

    // Create SrsDash object
    SrsUniquePtr<SrsDash> dash(new SrsDash());

    // Create mock dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));
    MockDashController *mock_controller = new MockDashController();
    MockOriginHub *mock_hub = new MockOriginHub();

    // Inject mock config into SrsDash
    dash->config_ = mock_config.get();

    // Inject mock controller into SrsDash
    srs_freep(dash->controller_);
    dash->controller_ = mock_controller;

    // Test initialize() - major use scenario step 1
    // This method is called AFTER the source has been added to the source pool
    // All field initialization MUST NOT cause coroutine context switches
    HELPER_EXPECT_SUCCESS(dash->initialize(mock_hub, mock_req.get()));

    // Verify initialize() set the hub and request correctly
    EXPECT_TRUE(dash->hub_ == mock_hub);
    EXPECT_TRUE(dash->req_ == mock_req.get());

    // Verify initialize() called controller->initialize()
    EXPECT_TRUE(mock_controller->initialize_called_);

    // Test cleanup_delay() when dash_dispose is disabled (returns 0)
    // Should return 0 when dash_dispose is disabled
    srs_utime_t delay = dash->cleanup_delay();
    EXPECT_EQ(0, delay);

    // Test cleanup_delay() when dash_dispose is enabled
    // Configure mock to return 120 seconds for dash_dispose
    mock_config->dash_dispose_ = 120 * SRS_UTIME_SECONDS;
    delay = dash->cleanup_delay();
    // Should return dash_dispose * 1.1 = 120 * 1.1 = 132 seconds
    EXPECT_EQ(132 * SRS_UTIME_SECONDS, delay);

    // Test dispose() when not enabled and dash_dispose is 0 - should not call on_unpublish or controller->dispose()
    mock_config->dash_dispose_ = 0;
    dash->dispose();
    EXPECT_FALSE(mock_controller->on_unpublish_called_);
    EXPECT_FALSE(mock_controller->dispose_called_); // dash_dispose is 0, so controller->dispose() should not be called

    // Test dispose() when enabled but dash_dispose is 0 - should call on_unpublish but not controller->dispose()
    dash->enabled_ = true;
    mock_config->dash_dispose_ = 0;
    dash->dispose();
    EXPECT_TRUE(mock_controller->on_unpublish_called_);
    EXPECT_FALSE(mock_controller->dispose_called_); // dash_dispose is 0, so controller->dispose() should not be called

    // Reset flags for next test
    mock_controller->on_unpublish_called_ = false;
    mock_controller->dispose_called_ = false;

    // Test dispose() when not enabled but dash_dispose is non-zero - should call controller->dispose() but not on_unpublish
    dash->enabled_ = false;
    mock_config->dash_dispose_ = 120 * SRS_UTIME_SECONDS;
    dash->dispose();
    EXPECT_FALSE(mock_controller->on_unpublish_called_); // enabled is false, so on_unpublish should not be called
    EXPECT_TRUE(mock_controller->dispose_called_); // dash_dispose is non-zero, so controller->dispose() should be called

    // Reset flags for next test
    mock_controller->on_unpublish_called_ = false;
    mock_controller->dispose_called_ = false;

    // Test dispose() when enabled and dash_dispose is non-zero - should call both on_unpublish and controller->dispose()
    dash->enabled_ = true;
    mock_config->dash_dispose_ = 120 * SRS_UTIME_SECONDS;
    dash->dispose();

    // Verify dispose() called on_unpublish when enabled
    EXPECT_TRUE(mock_controller->on_unpublish_called_);

    // Verify dispose() called controller->dispose() when dash_dispose is enabled
    EXPECT_TRUE(mock_controller->dispose_called_);

    // Clean up - set to NULL to avoid double-free
    dash->config_ = NULL;
    dash->controller_ = NULL;
    srs_freep(mock_controller);
    srs_freep(mock_hub);
}

// Test SrsDash publish lifecycle: on_publish, on_audio, on_video, on_unpublish
// This test covers the major use scenario for SrsDash streaming workflow
VOID TEST(DashTest, PublishLifecycleWithAudioVideo)
{
    srs_error_t err;

    // Create SrsDash object
    SrsUniquePtr<SrsDash> dash(new SrsDash());

    // Create mock dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));
    MockDashController *mock_controller = new MockDashController();
    MockOriginHub *mock_hub = new MockOriginHub();

    // Inject mock config into SrsDash
    dash->config_ = mock_config.get();

    // Inject mock controller into SrsDash
    srs_freep(dash->controller_);
    dash->controller_ = mock_controller;

    // Initialize the dash object
    HELPER_EXPECT_SUCCESS(dash->initialize(mock_hub, mock_req.get()));

    // Test on_publish() when DASH is disabled - should not enable
    HELPER_EXPECT_SUCCESS(dash->on_publish());
    EXPECT_FALSE(dash->enabled_);
    EXPECT_FALSE(mock_controller->on_publish_called_);

    // Enable DASH in config
    mock_config->dash_enabled_ = true;

    // Test on_publish() when DASH is enabled - major use scenario step 1
    HELPER_EXPECT_SUCCESS(dash->on_publish());

    // Verify on_publish() enabled DASH and called controller
    EXPECT_TRUE(dash->enabled_);
    EXPECT_TRUE(dash->disposable_);
    EXPECT_TRUE(mock_controller->on_publish_called_);

    // Test on_publish() again - should prevent duplicated publish
    mock_controller->on_publish_called_ = false;
    HELPER_EXPECT_SUCCESS(dash->on_publish());
    EXPECT_FALSE(mock_controller->on_publish_called_); // Should not call again

    // Create audio packet and format - major use scenario step 2
    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->timestamp_ = 1000;
    audio_packet->message_type_ = SrsFrameTypeAudio;

    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;

    // Test on_audio() with valid audio codec - major use scenario step 2
    HELPER_EXPECT_SUCCESS(dash->on_audio(audio_packet.get(), format.get()));

    // Verify on_audio() called controller
    // Note: MockDashController::on_audio() returns srs_success, so we just verify no error

    // Create video packet and format - major use scenario step 3
    SrsUniquePtr<SrsMediaPacket> video_packet(new SrsMediaPacket());
    video_packet->timestamp_ = 2000;
    video_packet->message_type_ = SrsFrameTypeVideo;

    format->vcodec_ = new SrsVideoCodecConfig();
    format->vcodec_->id_ = SrsVideoCodecIdAVC;

    // Test on_video() with valid video codec - major use scenario step 3
    HELPER_EXPECT_SUCCESS(dash->on_video(video_packet.get(), format.get()));

    // Verify on_video() called controller
    // Note: MockDashController::on_video() returns srs_success, so we just verify no error

    // Test on_audio() when not enabled - should return success without processing
    dash->enabled_ = false;
    HELPER_EXPECT_SUCCESS(dash->on_audio(audio_packet.get(), format.get()));

    // Test on_video() when not enabled - should return success without processing
    HELPER_EXPECT_SUCCESS(dash->on_video(video_packet.get(), format.get()));

    // Re-enable for unpublish test
    dash->enabled_ = true;

    // Test on_audio() with NULL audio codec - should return success without processing
    SrsUniquePtr<SrsFormat> format_no_audio(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format_no_audio->initialize());
    HELPER_EXPECT_SUCCESS(dash->on_audio(audio_packet.get(), format_no_audio.get()));

    // Test on_video() with NULL video codec - should return success without processing
    SrsUniquePtr<SrsFormat> format_no_video(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format_no_video->initialize());
    HELPER_EXPECT_SUCCESS(dash->on_video(video_packet.get(), format_no_video.get()));

    // Test on_unpublish() - major use scenario step 4
    dash->on_unpublish();

    // Verify on_unpublish() disabled DASH and called controller
    EXPECT_FALSE(dash->enabled_);
    EXPECT_TRUE(mock_controller->on_unpublish_called_);

    // Test on_unpublish() again - should prevent duplicated unpublish
    mock_controller->on_unpublish_called_ = false;
    dash->on_unpublish();
    EXPECT_FALSE(mock_controller->on_unpublish_called_); // Should not call again

    // Clean up - set to NULL to avoid double-free
    dash->config_ = NULL;
    dash->controller_ = NULL;
    srs_freep(mock_controller);
    srs_freep(mock_hub);
}

// A publisher idle past dash_dispose has its DASH disposed while it stays
// published. When it sends media again, DASH starts again, fed with the
// sequence headers, and writes the media.
VOID TEST(DashTest, DisposedIdlePublisherResumesOnMedia)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    mock_config->dash_enabled_ = true;
    mock_config->dash_dispose_ = 1 * SRS_UTIME_SECONDS;
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));
    MockOriginHub mock_hub;

    SrsUniquePtr<SrsDash> dash(new SrsDash());
    dash->config_ = mock_config.get();
    srs_freep(dash->controller_);
    MockDashController *mock_controller = new MockDashController();
    dash->controller_ = mock_controller;
    HELPER_EXPECT_SUCCESS(dash->initialize(&mock_hub, mock_req.get()));

    HELPER_EXPECT_SUCCESS(dash->on_publish());

    // The publisher sends nothing past dash_dispose, and the cycle disposes
    // its DASH.
    dash->last_update_time_ = 1;
    HELPER_EXPECT_SUCCESS(dash->cycle());
    EXPECT_FALSE(dash->enabled_);
    EXPECT_TRUE(mock_controller->on_unpublish_called_);
    EXPECT_TRUE(mock_controller->dispose_called_);

    SrsUniquePtr<SrsMediaPacket> video_packet(new SrsMediaPacket());
    video_packet->timestamp_ = 2000;
    video_packet->message_type_ = SrsFrameTypeVideo;
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());
    format->vcodec_ = new SrsVideoCodecConfig();
    format->vcodec_->id_ = SrsVideoCodecIdAVC;

    // The publisher sends media again.
    mock_controller->on_publish_called_ = false;
    HELPER_EXPECT_SUCCESS(dash->on_video(video_packet.get(), format.get()));
    EXPECT_TRUE(dash->enabled_);
    EXPECT_TRUE(dash->disposable_);
    EXPECT_TRUE(mock_controller->on_publish_called_);
    EXPECT_EQ(1, mock_hub.on_dash_request_sh_count_);
    EXPECT_EQ(1, mock_controller->on_video_count_);

    // The next media goes on, with no more sequence headers.
    HELPER_EXPECT_SUCCESS(dash->on_video(video_packet.get(), format.get()));
    EXPECT_EQ(1, mock_hub.on_dash_request_sh_count_);
    EXPECT_EQ(2, mock_controller->on_video_count_);

    dash->on_unpublish();
    dash->config_ = NULL;
}

// DASH turned off by an unpublish, by an error of its media, or by the
// dispose at the server's quit stays off: only the cycle's dispose of an idle
// publisher resumes on media.
VOID TEST(DashTest, DisposedThenUnpublishedStaysOff)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    mock_config->dash_enabled_ = true;
    mock_config->dash_dispose_ = 1 * SRS_UTIME_SECONDS;
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));
    MockOriginHub mock_hub;

    SrsUniquePtr<SrsDash> dash(new SrsDash());
    dash->config_ = mock_config.get();
    srs_freep(dash->controller_);
    MockDashController *mock_controller = new MockDashController();
    dash->controller_ = mock_controller;
    HELPER_EXPECT_SUCCESS(dash->initialize(&mock_hub, mock_req.get()));

    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->timestamp_ = 1000;
    audio_packet->message_type_ = SrsFrameTypeAudio;
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;

    // Disposed while idle, then the publisher leaves.
    HELPER_EXPECT_SUCCESS(dash->on_publish());
    dash->last_update_time_ = 1;
    HELPER_EXPECT_SUCCESS(dash->cycle());
    EXPECT_TRUE(mock_controller->dispose_called_);
    dash->on_unpublish();
    mock_controller->on_publish_called_ = false;
    HELPER_EXPECT_SUCCESS(dash->on_audio(audio_packet.get(), format.get()));
    EXPECT_FALSE(dash->enabled_);
    EXPECT_FALSE(mock_controller->on_publish_called_);
    EXPECT_EQ(0, mock_controller->on_audio_count_);

    // Turned off without a dispose, as the hub does for an error of its media.
    HELPER_EXPECT_SUCCESS(dash->on_publish());
    dash->on_unpublish();
    mock_controller->on_publish_called_ = false;
    HELPER_EXPECT_SUCCESS(dash->on_audio(audio_packet.get(), format.get()));
    EXPECT_FALSE(dash->enabled_);
    EXPECT_FALSE(mock_controller->on_publish_called_);
    EXPECT_EQ(0, mock_controller->on_audio_count_);

    // Disposed while published, as the server does when it quits.
    HELPER_EXPECT_SUCCESS(dash->on_publish());
    dash->dispose();
    mock_controller->on_publish_called_ = false;
    HELPER_EXPECT_SUCCESS(dash->on_audio(audio_packet.get(), format.get()));
    EXPECT_FALSE(dash->enabled_);
    EXPECT_FALSE(mock_controller->on_publish_called_);
    EXPECT_EQ(0, mock_controller->on_audio_count_);
    EXPECT_EQ(0, mock_hub.on_dash_request_sh_count_);

    dash->config_ = NULL;
}

// Test SrsHls dispose behavior after disabled
// This test covers dispose() logic when enabled_ is false but hls_dispose is configured
VOID TEST(HlsTest, LifecycleDisposeAfterDisabled)
{
    // Create SrsHls object
    SrsUniquePtr<SrsHls> hls(new SrsHls());

    // Create mock dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));
    MockHlsController *mock_controller = new MockHlsController();
    MockOriginHub *mock_hub = new MockOriginHub();

    // Inject mock config into SrsHls
    hls->config_ = mock_config.get();

    // Inject mock controller into SrsHls
    srs_freep(hls->controller_);
    hls->controller_ = mock_controller;

    // Inject mock request
    hls->req_ = mock_req.get();

    // Test dispose() when not enabled and hls_dispose is 0 - should not call on_unpublish or controller->dispose()
    mock_config->hls_dispose_ = 0;
    hls->dispose();
    EXPECT_EQ(0, mock_controller->on_unpublish_count_);
    EXPECT_EQ(0, mock_controller->dispose_count_); // hls_dispose is 0, so controller->dispose() should not be called

    // Test dispose() when enabled but hls_dispose is 0 - should call on_unpublish but not controller->dispose()
    hls->enabled_ = true;
    mock_config->hls_dispose_ = 0;
    hls->dispose();
    EXPECT_EQ(1, mock_controller->on_unpublish_count_);
    EXPECT_EQ(0, mock_controller->dispose_count_); // hls_dispose is 0, so controller->dispose() should not be called

    // Reset counts for next test
    mock_controller->reset();

    // Test dispose() when not enabled but hls_dispose is non-zero - should call controller->dispose() but not on_unpublish
    hls->enabled_ = false;
    mock_config->hls_dispose_ = 120 * SRS_UTIME_SECONDS;
    hls->dispose();
    EXPECT_EQ(0, mock_controller->on_unpublish_count_); // enabled is false, so on_unpublish should not be called
    EXPECT_EQ(1, mock_controller->dispose_count_); // hls_dispose is non-zero, so controller->dispose() should be called

    // Reset counts for next test
    mock_controller->reset();

    // Test dispose() when enabled and hls_dispose is non-zero - should call both on_unpublish and controller->dispose()
    hls->enabled_ = true;
    mock_config->hls_dispose_ = 120 * SRS_UTIME_SECONDS;
    hls->dispose();

    // Verify dispose() called on_unpublish when enabled
    EXPECT_EQ(1, mock_controller->on_unpublish_count_);

    // Verify dispose() called controller->dispose() when hls_dispose is enabled
    EXPECT_EQ(1, mock_controller->dispose_count_);

    // Clean up - set to NULL to avoid double-free
    hls->config_ = NULL;
    hls->controller_ = NULL;
    hls->req_ = NULL;
    srs_freep(mock_controller);
    srs_freep(mock_hub);
}

// Test SrsFragmentedMp4 delegation to fragment_ member
// This test covers the major use scenario for SrsFragmentedMp4 ISrsFragment interface delegation
VOID TEST(FragmentedMp4Test, FragmentDelegation)
{
    srs_error_t err;

    // Create SrsFragmentedMp4 object
    SrsUniquePtr<SrsFragmentedMp4> fmp4(new SrsFragmentedMp4());

    // Create mock fragment
    MockFragment *mock_fragment = new MockFragment();

    // Inject mock fragment
    srs_freep(fmp4->fragment_);
    fmp4->fragment_ = mock_fragment;

    // Test set_path delegation
    fmp4->set_path("/tmp/test_fragment.m4s");
    EXPECT_TRUE(mock_fragment->set_path_called_);
    EXPECT_STREQ("/tmp/test_fragment.m4s", mock_fragment->path_.c_str());

    // Test tmppath delegation
    string tmp = fmp4->tmppath();
    EXPECT_TRUE(mock_fragment->tmppath_called_);
    EXPECT_STREQ("/tmp/test.mp4.tmp", tmp.c_str());

    // Test rename delegation
    HELPER_EXPECT_SUCCESS(fmp4->rename());
    EXPECT_TRUE(mock_fragment->rename_called_);

    // Test append delegation
    fmp4->append(54321);
    EXPECT_TRUE(mock_fragment->append_called_);
    EXPECT_EQ(54321, mock_fragment->append_dts_);

    // Test create_dir delegation
    HELPER_EXPECT_SUCCESS(fmp4->create_dir());
    EXPECT_TRUE(mock_fragment->create_dir_called_);

    // Test set_number delegation
    fmp4->set_number(200);
    EXPECT_TRUE(mock_fragment->set_number_called_);
    EXPECT_EQ(200, mock_fragment->number_);

    // Test number delegation
    uint64_t num = fmp4->number();
    EXPECT_TRUE(mock_fragment->number_called_);
    EXPECT_EQ(200, num);

    // Test duration delegation
    mock_fragment->duration_ = 10 * SRS_UTIME_SECONDS;
    srs_utime_t dur = fmp4->duration();
    EXPECT_TRUE(mock_fragment->duration_called_);
    EXPECT_EQ(10 * SRS_UTIME_SECONDS, dur);

    // Test unlink_tmpfile delegation
    HELPER_EXPECT_SUCCESS(fmp4->unlink_tmpfile());
    EXPECT_TRUE(mock_fragment->unlink_tmpfile_called_);

    // Test get_start_dts delegation
    mock_fragment->start_dts_ = 98765 * SRS_UTIME_MILLISECONDS;
    srs_utime_t start_dts = fmp4->get_start_dts();
    EXPECT_TRUE(mock_fragment->get_start_dts_called_);
    EXPECT_EQ(98765 * SRS_UTIME_MILLISECONDS, start_dts);

    // Test unlink_file delegation
    HELPER_EXPECT_SUCCESS(fmp4->unlink_file());
    EXPECT_TRUE(mock_fragment->unlink_file_called_);

    // Clean up - set to NULL to avoid double-free
    fmp4->fragment_ = NULL;
    srs_freep(mock_fragment);
}

// Mock SrsRtcConnection implementation
MockRtcConnectionForNackApi::MockRtcConnectionForNackApi()
{
    simulate_nack_drop_value_ = 0;
    simulate_nack_drop_called_ = false;
}

MockRtcConnectionForNackApi::~MockRtcConnectionForNackApi()
{
}

void MockRtcConnectionForNackApi::simulate_nack_drop(int nn)
{
    simulate_nack_drop_called_ = true;
    simulate_nack_drop_value_ = nn;
}

// Mock ISrsRtcApiServer implementation
MockRtcApiServer::MockRtcApiServer()
{
    create_session_called_ = false;
    session_id_ = "test-session-id-12345";
    local_sdp_str_ = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=SRS\r\nt=0 0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 H264/90000\r\n";
    mock_connection_ = NULL;
    find_username_ = "";
}

MockRtcApiServer::~MockRtcApiServer()
{
    srs_freep(mock_connection_);
}

srs_error_t MockRtcApiServer::create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection **psession)
{
    create_session_called_ = true;

    // Set the local SDP string, session_id and token in ruc
    // Note: In real code, these are set from the session object at lines 571-572,
    // but we set them directly here to avoid needing a full mock session
    ruc->local_sdp_str_ = local_sdp_str_;
    ruc->session_id_ = session_id_;
    ruc->token_ = "test-token-67890";

    // Return NULL session pointer
    // Note: This will cause the code to crash at line 571-572 where it tries to access session->username()
    // This is a limitation of the current test - we would need a full mock SrsRtcConnection to avoid this
    *psession = NULL;

    return srs_success;
}

ISrsRtcConnection *MockRtcApiServer::find_rtc_session_by_username(const std::string &ufrag)
{
    find_username_ = ufrag;
    // Return NULL to simulate session not found (easier to test than full mock)
    return NULL;
}

// Mock ISrsStatistic implementation
MockStatisticForRtcApi::MockStatisticForRtcApi()
{
    server_id_ = "test-server-id";
    service_id_ = "test-service-id";
    service_pid_ = "12345";
}

MockStatisticForRtcApi::~MockStatisticForRtcApi()
{
}

void MockStatisticForRtcApi::on_disconnect(std::string id, srs_error_t err)
{
}

srs_error_t MockStatisticForRtcApi::on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type)
{
    return srs_success;
}

srs_error_t MockStatisticForRtcApi::on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height)
{
    return srs_success;
}

srs_error_t MockStatisticForRtcApi::on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate,
                                                  SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    return srs_success;
}

void MockStatisticForRtcApi::on_stream_publish(ISrsRequest *req, std::string publisher_id)
{
}

void MockStatisticForRtcApi::on_stream_close(ISrsRequest *req)
{
}

void MockStatisticForRtcApi::kbps_add_delta(std::string id, ISrsKbpsDelta *delta)
{
}

void MockStatisticForRtcApi::kbps_sample()
{
}

srs_error_t MockStatisticForRtcApi::on_video_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

srs_error_t MockStatisticForRtcApi::on_audio_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

std::string MockStatisticForRtcApi::server_id()
{
    return server_id_;
}

std::string MockStatisticForRtcApi::service_id()
{
    return service_id_;
}

std::string MockStatisticForRtcApi::service_pid()
{
    return service_pid_;
}

SrsStatisticVhost *MockStatisticForRtcApi::find_vhost_by_id(std::string vid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForRtcApi::find_stream(std::string sid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForRtcApi::find_stream_by_url(std::string url)
{
    return NULL;
}

SrsStatisticClient *MockStatisticForRtcApi::find_client(std::string client_id)
{
    return NULL;
}

srs_error_t MockStatisticForRtcApi::dumps_vhosts(SrsJsonArray *arr)
{
    return srs_success;
}

srs_error_t MockStatisticForRtcApi::dumps_streams(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForRtcApi::dumps_clients(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForRtcApi::dumps_forwards(SrsJsonArray *arr, std::string stream_id, int start, int count)
{
    return srs_success;
}

int MockStatisticForRtcApi::count_forwards(std::string stream_id)
{
    return 0;
}

SrsStatisticForward *MockStatisticForRtcApi::find_forward(std::string forward_id)
{
    return NULL;
}

void MockStatisticForRtcApi::on_forward_start(std::string id, ISrsRequest *req, std::string url)
{
}

void MockStatisticForRtcApi::on_forward_state(std::string id, SrsForwardState state)
{
}

void MockStatisticForRtcApi::on_forward_stop(std::string id)
{
}

srs_error_t MockStatisticForRtcApi::dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    send_bytes = 0;
    recv_bytes = 0;
    nstreams = 0;
    nclients = 0;
    total_nclients = 0;
    nerrs = 0;
    return srs_success;
}

// Mock ISrsHttpMessage implementation
MockHttpMessageForRtcApi::MockHttpMessageForRtcApi() : SrsHttpMessage()
{
    mock_conn_ = new MockHttpConn();
    set_connection(mock_conn_);
    body_content_ = "";
    method_ = SRS_CONSTS_HTTP_POST; // Default to POST
}

MockHttpMessageForRtcApi::~MockHttpMessageForRtcApi()
{
    srs_freep(mock_conn_);
}

srs_error_t MockHttpMessageForRtcApi::body_read_all(std::string &body)
{
    body = body_content_;
    return srs_success;
}

std::string MockHttpMessageForRtcApi::query_get(std::string key)
{
    if (query_params_.find(key) != query_params_.end()) {
        return query_params_[key];
    }
    return "";
}

uint8_t MockHttpMessageForRtcApi::method()
{
    return method_;
}

void MockHttpMessageForRtcApi::set_method(uint8_t method)
{
    method_ = method;
}

// Mock ISrsAppConfig implementation for SrsGoApiRtcPlay::serve_http()
MockAppConfigForRtcPlay::MockAppConfigForRtcPlay()
{
    dtls_role_ = "passive";
    dtls_version_ = "auto";
    rtc_server_enabled_ = true;
    rtc_enabled_ = true;
    vhost_is_edge_ = false;
    rtc_from_rtmp_ = false;
    http_hooks_enabled_ = false;
    on_play_directive_ = NULL;
}

MockAppConfigForRtcPlay::~MockAppConfigForRtcPlay()
{
    srs_freep(on_play_directive_);
}

std::string MockAppConfigForRtcPlay::get_rtc_dtls_role(std::string vhost)
{
    return dtls_role_;
}

std::string MockAppConfigForRtcPlay::get_rtc_dtls_version(std::string vhost)
{
    return dtls_version_;
}

bool MockAppConfigForRtcPlay::get_rtc_server_enabled()
{
    return rtc_server_enabled_;
}

bool MockAppConfigForRtcPlay::get_rtc_enabled(std::string vhost)
{
    return rtc_enabled_;
}

bool MockAppConfigForRtcPlay::get_vhost_is_edge(std::string vhost)
{
    return vhost_is_edge_;
}

bool MockAppConfigForRtcPlay::get_rtc_from_rtmp(std::string vhost)
{
    return rtc_from_rtmp_;
}

bool MockAppConfigForRtcPlay::get_vhost_http_hooks_enabled(std::string vhost)
{
    return http_hooks_enabled_;
}

SrsConfDirective *MockAppConfigForRtcPlay::get_vhost_on_play(std::string vhost)
{
    return on_play_directive_;
}

// Mock ISrsHttpHooks implementation for SrsGoApiRtcPlay::serve_http()
MockHttpHooksForRtcPlay::MockHttpHooksForRtcPlay()
{
    on_play_count_ = 0;
}

MockHttpHooksForRtcPlay::~MockHttpHooksForRtcPlay()
{
}

srs_error_t MockHttpHooksForRtcPlay::on_connect(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForRtcPlay::on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes)
{
}

srs_error_t MockHttpHooksForRtcPlay::on_publish(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForRtcPlay::on_unpublish(std::string url, ISrsRequest *req)
{
}

srs_error_t MockHttpHooksForRtcPlay::on_play(std::string url, ISrsRequest *req)
{
    on_play_count_++;
    on_play_calls_.push_back(std::make_pair(url, req));
    return srs_success;
}

void MockHttpHooksForRtcPlay::on_stop(std::string url, ISrsRequest *req)
{
}

srs_error_t MockHttpHooksForRtcPlay::on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file)
{
    return srs_success;
}

srs_error_t MockHttpHooksForRtcPlay::on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                                            std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration)
{
    return srs_success;
}

srs_error_t MockHttpHooksForRtcPlay::on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify)
{
    return srs_success;
}

srs_error_t MockHttpHooksForRtcPlay::discover_co_workers(std::string url, std::string &host, int &port)
{
    return srs_success;
}

srs_error_t MockHttpHooksForRtcPlay::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    return srs_success;
}

// Mock SrsRtcConnection implementation for SrsGoApiRtcPlay::serve_http()
MockRtcConnectionForPlay::MockRtcConnectionForPlay()
{
    username_ = "test-username-12345";
    token_ = "test-token-67890";
}

MockRtcConnectionForPlay::~MockRtcConnectionForPlay()
{
}

std::string MockRtcConnectionForPlay::username()
{
    return username_;
}

std::string MockRtcConnectionForPlay::token()
{
    return token_;
}

// Mock ISrsRtcApiServer implementation for SrsGoApiRtcPlay::serve_http()
MockRtcApiServerForPlay::MockRtcApiServerForPlay()
{
    create_session_called_ = false;
    mock_connection_ = new MockRtcConnectionForPlay();
}

MockRtcApiServerForPlay::~MockRtcApiServerForPlay()
{
    srs_freep(mock_connection_);
}

srs_error_t MockRtcApiServerForPlay::create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection **psession)
{
    create_session_called_ = true;

    // Create a real SrsRtcConnection object for testing
    // Note: We need to pass a valid exec and cid
    SrsContextId cid;
    SrsRtcConnection *session = new SrsRtcConnection(NULL, cid);

    // Set the username and token directly (accessible in utests)
    session->username_ = mock_connection_->username_;
    session->token_ = mock_connection_->token_;

    *psession = session;
    return srs_success;
}

ISrsRtcConnection *MockRtcApiServerForPlay::find_rtc_session_by_username(const std::string &ufrag)
{
    return NULL;
}

VOID TEST(RtcApiPlayTest, ServeHttpSuccess)
{
    // This test covers the major use scenario for SrsGoApiRtcPlay::serve_http():
    // 1. Client sends POST request with invalid JSON body (missing required "sdp" field)
    // 2. Server attempts to parse the request body
    // 3. Server validates required fields and returns error
    // 4. Server returns JSON response with error code (HTTP status is still 200)
    //
    // This tests the error handling path which is easier to test than the full success
    // path that would require mocking many complex dependencies (config, sources, hooks, etc.)
    srs_error_t err = srs_success;

    // Create mock dependencies
    SrsUniquePtr<MockRtcApiServer> mock_server(new MockRtcApiServer());
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());

    // Create the API handler
    SrsUniquePtr<SrsGoApiRtcPlay> api(new SrsGoApiRtcPlay(mock_server.get()));

    // Prepare request body with INVALID JSON - missing required "sdp" field
    SrsJsonObject req_json;
    req_json.set("streamurl", SrsJsonAny::str("webrtc://localhost/live/livestream"));
    req_json.set("clientip", SrsJsonAny::str("192.168.1.100"));
    // Note: "sdp" field is intentionally missing to trigger error

    mock_request->body_content_ = req_json.dumps();

    // Call serve_http - should fail due to missing "sdp" field
    // Expected behavior:
    // 1. Parse JSON body successfully
    // 2. Try to get "sdp" field - fails because it's missing
    // 3. Return error from do_serve_http
    // 4. serve_http catches error and returns JSON with error code
    HELPER_EXPECT_SUCCESS(api->serve_http(mock_writer.get(), mock_request.get()));

    // Get the HTTP response
    string response = string(mock_writer->io.out_buffer.bytes(), mock_writer->io.out_buffer.length());
    EXPECT_FALSE(response.empty());

    // Verify the response contains a non-zero error code (not ERROR_SUCCESS)
    // The exact error code depends on the implementation, but it should not be 0
    EXPECT_TRUE(response.find("\"code\":0") == std::string::npos);

    // Verify the response is valid JSON with a "code" field
    EXPECT_TRUE(response.find("\"code\":") != std::string::npos);
}

// Test SrsGoApiRtcPlay::http_hooks_on_play() to verify HTTP hooks are called correctly
// when playing WebRTC streams. This covers the major use scenario where HTTP hooks are enabled
// and multiple hook URLs are configured for on_play events.
VOID TEST(SrsGoApiRtcPlayTest, HttpHooksOnPlaySuccess)
{
    srs_error_t err = srs_success;

    // Create mock RTC API server
    SrsUniquePtr<MockRtcApiServer> mock_server(new MockRtcApiServer());

    // Create SrsGoApiRtcPlay instance
    SrsUniquePtr<SrsGoApiRtcPlay> api(new SrsGoApiRtcPlay(mock_server.get()));

    // Create mock config with HTTP hooks enabled
    MockAppConfigForHttpHooksOnPlay *mock_config = new MockAppConfigForHttpHooksOnPlay();
    mock_config->http_hooks_enabled_ = true;

    // Create on_play directive with two hook URLs
    mock_config->on_play_directive_ = new SrsConfDirective();
    mock_config->on_play_directive_->name_ = "on_play";
    mock_config->on_play_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/rtc/play");
    mock_config->on_play_directive_->args_.push_back("http://localhost:8085/api/v1/rtc/play2");

    // Create mock hooks
    MockHttpHooksForOnPlay *mock_hooks = new MockHttpHooksForOnPlay();

    // Inject mocks into API
    api->config_ = mock_config;
    api->hooks_ = mock_hooks;

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "livestream"));

    // Call http_hooks_on_play
    HELPER_EXPECT_SUCCESS(api->http_hooks_on_play(req.get()));

    // Verify hooks were called twice (once for each URL)
    EXPECT_EQ(2, mock_hooks->on_play_count_);
    EXPECT_EQ(2, (int)mock_hooks->on_play_calls_.size());

    // Verify the URLs were correct
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/rtc/play", mock_hooks->on_play_calls_[0].first.c_str());
    EXPECT_STREQ("http://localhost:8085/api/v1/rtc/play2", mock_hooks->on_play_calls_[1].first.c_str());

    // Verify the request was passed correctly
    EXPECT_EQ(req.get(), mock_hooks->on_play_calls_[0].second);
    EXPECT_EQ(req.get(), mock_hooks->on_play_calls_[1].second);

    // Clean up - set to NULL to avoid double-free
    api->config_ = NULL;
    api->hooks_ = NULL;
    srs_freep(mock_config);
    srs_freep(mock_hooks);
}

// Test SrsGoApiRtcPublish::serve_http() to verify the major use scenario for WebRTC publish API.
// This test covers the error handling path:
// 1. Client sends POST request with invalid JSON body (missing required "sdp" field)
// 2. Server attempts to parse the request body
// 3. Server validates required fields and returns error
// 4. Server returns JSON response with error code (HTTP status is still 200)
//
// This tests the error handling path which is easier to test than the full success
// path that would require mocking many complex dependencies (config, sources, hooks, etc.)
VOID TEST(SrsGoApiRtcPublishTest, ServeHttpSuccess)
{
    srs_error_t err = srs_success;

    // Create mock dependencies
    SrsUniquePtr<MockRtcApiServer> mock_server(new MockRtcApiServer());
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());

    // Create the API handler
    SrsUniquePtr<SrsGoApiRtcPublish> api(new SrsGoApiRtcPublish(mock_server.get()));

    // Prepare request body with INVALID JSON - missing required "sdp" field
    SrsJsonObject req_json;
    req_json.set("streamurl", SrsJsonAny::str("webrtc://localhost/live/livestream"));
    req_json.set("clientip", SrsJsonAny::str("192.168.1.100"));
    // Note: "sdp" field is intentionally missing to trigger error

    mock_request->body_content_ = req_json.dumps();

    // Call serve_http - should fail due to missing "sdp" field
    // Expected behavior:
    // 1. Parse JSON body successfully
    // 2. Try to get "sdp" field - fails because it's missing
    // 3. Return error from do_serve_http
    // 4. serve_http catches error and returns JSON with error code
    HELPER_EXPECT_SUCCESS(api->serve_http(mock_writer.get(), mock_request.get()));

    // Get the HTTP response
    string response = string(mock_writer->io.out_buffer.bytes(), mock_writer->io.out_buffer.length());
    EXPECT_FALSE(response.empty());

    // Verify the response contains a non-zero error code (not ERROR_SUCCESS)
    // The exact error code depends on the implementation, but it should not be 0
    EXPECT_TRUE(response.find("\"code\":0") == std::string::npos);

    // Verify the response is valid JSON with a "code" field
    EXPECT_TRUE(response.find("\"code\":") != std::string::npos);
}

VOID TEST(SrsGoApiRtcPublishTest, HttpHooksOnPublishSuccess)
{
    srs_error_t err = srs_success;

    // Create mock RTC API server (NULL is acceptable for this test)
    ISrsRtcApiServer *mock_server = NULL;

    // Create SrsGoApiRtcPublish instance
    SrsUniquePtr<SrsGoApiRtcPublish> api(new SrsGoApiRtcPublish(mock_server));

    // Create mock config with HTTP hooks enabled
    MockAppConfigForHttpHooksOnPublish *mock_config = new MockAppConfigForHttpHooksOnPublish();
    mock_config->http_hooks_enabled_ = true;

    // Create on_publish directive with two hook URLs
    mock_config->on_publish_directive_ = new SrsConfDirective();
    mock_config->on_publish_directive_->name_ = "on_publish";
    mock_config->on_publish_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/rtc/publish");
    mock_config->on_publish_directive_->args_.push_back("http://localhost:8085/api/v1/rtc/publish");

    // Create mock hooks
    MockHttpHooksForOnPublish *mock_hooks = new MockHttpHooksForOnPublish();

    // Inject mocks into api
    api->config_ = mock_config;
    api->hooks_ = mock_hooks;

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("__defaultVhost__", "live", "livestream"));

    // Test the major use scenario: http_hooks_on_publish() with hooks enabled
    // This should:
    // 1. Check if HTTP hooks are enabled (they are)
    // 2. Get the on_publish directive from config
    // 3. Copy the hook URLs from the directive
    // 4. Call hooks_->on_publish() for each URL
    HELPER_EXPECT_SUCCESS(api->http_hooks_on_publish(req.get()));

    // Verify that on_publish was called twice (once for each URL)
    EXPECT_EQ(2, mock_hooks->on_publish_count_);
    EXPECT_EQ(2, (int)mock_hooks->on_publish_calls_.size());

    // Verify the first call
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/rtc/publish", mock_hooks->on_publish_calls_[0].first.c_str());
    EXPECT_TRUE(mock_hooks->on_publish_calls_[0].second == req.get());

    // Verify the second call
    EXPECT_STREQ("http://localhost:8085/api/v1/rtc/publish", mock_hooks->on_publish_calls_[1].first.c_str());
    EXPECT_TRUE(mock_hooks->on_publish_calls_[1].second == req.get());

    // Clean up injected dependencies to avoid double-free
    api->config_ = NULL;
    api->hooks_ = NULL;
    srs_freep(mock_config);
    srs_freep(mock_hooks);
}

// Test SrsGoApiRtcWhip::serve_http() to verify the major use scenario for WHIP DELETE request.
// This test covers the WHIP session termination flow:
// 1. Client sends DELETE request with session and token parameters
// 2. Server validates the token matches the session
// 3. Server expires the session
// 4. Server returns 200 OK with empty body
// Note: Testing the POST/publish flow requires extensive mocking of RTC session creation,
// so we focus on the DELETE flow which is simpler and still demonstrates WHIP functionality.
VOID TEST(SrsGoApiRtcWhipTest, ServeHttpDeleteSuccess)
{
    srs_error_t err = srs_success;

    // Create mock RTC API server
    SrsUniquePtr<MockRtcApiServer> mock_server(new MockRtcApiServer());

    // Create SrsGoApiRtcWhip instance
    SrsUniquePtr<SrsGoApiRtcWhip> whip(new SrsGoApiRtcWhip(mock_server.get()));

    // Create mock response writer
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());

    // Create mock HTTP message for WHIP DELETE request
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());

    // Set HTTP method to DELETE
    mock_request->set_method(SRS_CONSTS_HTTP_DELETE);

    // Set query parameters for WHIP DELETE request
    // Note: In real WHIP, these come from the Location header returned during POST
    mock_request->query_params_["session"] = "test-session-username";
    mock_request->query_params_["token"] = "test-token-12345";

    // Call serve_http for DELETE request
    // Expected behavior:
    // 1. Check if method is DELETE
    // 2. Get session and token from query parameters
    // 3. Find session by username (returns NULL in our mock)
    // 4. Since session is NULL, skip token validation and expiration
    // 5. Return 200 OK with empty body
    HELPER_EXPECT_SUCCESS(whip->serve_http(mock_writer.get(), mock_request.get()));

    // Verify response status is 200 OK
    EXPECT_EQ(SRS_CONSTS_HTTP_OK, mock_writer->w->status_);

    // Get the full HTTP response from the output buffer
    string response = string(mock_writer->io.out_buffer.bytes(), mock_writer->io.out_buffer.length());
    EXPECT_FALSE(response.empty());

    // Note: MockResponseWriter filters out Connection, Content-Type, and Location headers
    // so we can't verify them in the response. We just verify the basic structure.

    // Verify Content-Length is 0 (empty body for DELETE)
    EXPECT_TRUE(response.find("Content-Length: 0") != std::string::npos);

    // Verify the response has HTTP status line
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
}

// Test SrsGoApiRtcWhip::serve_http() to verify the major use scenario for WHIP POST request.
// This test covers the WHIP session creation flow (non-DELETE path):
// 1. Client sends POST request with SDP offer in body
// 2. Server processes the request via do_serve_http_with() which populates ruc.local_sdp_str_
// 3. Server returns 201 Created with SDP answer in body
// 4. Server includes Location header for subsequent DELETE request
// 5. Server sets Content-Type to application/sdp
VOID TEST(SrsGoApiRtcWhipTest, ServeHttpPostSuccess)
{
    srs_error_t err = srs_success;

    // Create mock RTC API server
    SrsUniquePtr<MockRtcApiServer> mock_server(new MockRtcApiServer());

    // Create testable WHIP handler that overrides do_serve_http_with
    class TestableWhip : public SrsGoApiRtcWhip
    {
    public:
        TestableWhip(ISrsRtcApiServer *server) : SrsGoApiRtcWhip(server) {}
        virtual srs_error_t do_serve_http_with(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc)
        {
            // Mock the do_serve_http_with behavior by populating the required fields
            ruc->local_sdp_str_ = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=SRS\r\nt=0 0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 H264/90000\r\n";
            ruc->session_id_ = "test-session-12345";
            ruc->token_ = "test-token-67890";
            ruc->req_->app_ = "live";
            ruc->req_->stream_ = "livestream";
            return srs_success;
        }
    };

    // Create testable WHIP instance
    SrsUniquePtr<TestableWhip> whip(new TestableWhip(mock_server.get()));

    // Create mock response writer
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());

    // Create mock HTTP message for WHIP POST request
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());

    // Set HTTP method to POST (default, not DELETE)
    mock_request->set_method(SRS_CONSTS_HTTP_POST);

    // Set SDP offer in request body
    mock_request->body_content_ = "v=0\r\no=- 0 0 IN IP4 192.168.1.100\r\ns=WebRTC\r\nt=0 0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 H264/90000\r\n";

    // Set query parameters for WHIP POST request
    mock_request->query_params_["app"] = "live";
    mock_request->query_params_["stream"] = "livestream";

    // Call serve_http for POST request
    // Expected behavior:
    // 1. Check if method is DELETE (no, it's POST)
    // 2. Call do_serve_http_with() which populates ruc.local_sdp_str_
    // 3. Set Content-Type to application/sdp
    // 4. Set Location header with session and token
    // 5. Return 201 Created with SDP answer in body
    HELPER_EXPECT_SUCCESS(whip->serve_http(mock_writer.get(), mock_request.get()));

    // Verify response status is 201 Created (required by WHIP spec)
    EXPECT_EQ(201, mock_writer->w->status_);

    // Get the full HTTP response from the output buffer
    string response = string(mock_writer->io.out_buffer.bytes(), mock_writer->io.out_buffer.length());
    EXPECT_FALSE(response.empty());

    // Verify the response has HTTP status line with 201
    EXPECT_TRUE(response.find("HTTP/1.1 201") != std::string::npos);

    // Note: MockResponseWriter filters out Content-Type and Location headers
    // so we can't verify them in the response. We just verify the basic structure.

    // Verify SDP answer is in the response body
    EXPECT_TRUE(response.find("v=0") != std::string::npos);
    EXPECT_TRUE(response.find("o=- 0 0 IN IP4 127.0.0.1") != std::string::npos);
    EXPECT_TRUE(response.find("s=SRS") != std::string::npos);
    EXPECT_TRUE(response.find("m=video 9 UDP/TLS/RTP/SAVPF 96") != std::string::npos);
}

// Test SrsGoApiRtcWhip::do_serve_http_with() - major use scenario for WHIP request parsing
// This test covers the core parsing and validation logic of do_serve_http_with method:
// 1. Read SDP offer from request body
// 2. Extract client IP from connection (with proxy IP override support)
// 3. Parse query parameters (eip, codec, app, stream, action, ice-ufrag, ice-pwd, encrypt, dtls)
// 4. Populate SrsRtcUserConfig with request information
// 5. Validate ICE credentials length
// 6. Determine publish vs play action based on query parameter or path
// 7. Parse remote SDP and store in ruc
//
// Note: This test uses a testable subclass that overrides the publish/play handlers
// to avoid the complexity of full WebRTC session creation and SDP negotiation.
VOID TEST(SrsGoApiRtcWhipTest, DoServeHttpPublishSuccess)
{
    srs_error_t err = srs_success;

    // Create mock dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockRtcApiServerForPlay> mock_server(new MockRtcApiServerForPlay());

    // Create testable WHIP handler that overrides publish/play handlers
    class TestableWhipForParsing : public SrsGoApiRtcWhip
    {
    public:
        bool publish_called_;
        bool play_called_;
        SrsRtcUserConfig *captured_ruc_;

        TestableWhipForParsing(ISrsRtcApiServer *server) : SrsGoApiRtcWhip(server)
        {
            publish_called_ = false;
            play_called_ = false;
            captured_ruc_ = NULL;

            // Replace publish and play handlers with mocks
            srs_freep(publish_);
            srs_freep(play_);
            publish_ = new MockPublishHandler(this);
            play_ = new MockPlayHandler(this);
        }

        class MockPublishHandler : public SrsGoApiRtcPublish
        {
        public:
            TestableWhipForParsing *parent_;
            MockPublishHandler(TestableWhipForParsing *p) : SrsGoApiRtcPublish(NULL), parent_(p) {}
            virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc)
            {
                parent_->publish_called_ = true;
                parent_->captured_ruc_ = ruc;
                return srs_success;
            }
        };

        class MockPlayHandler : public SrsGoApiRtcPlay
        {
        public:
            TestableWhipForParsing *parent_;
            MockPlayHandler(TestableWhipForParsing *p) : SrsGoApiRtcPlay(NULL), parent_(p) {}
            virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc)
            {
                parent_->play_called_ = true;
                parent_->captured_ruc_ = ruc;
                return srs_success;
            }
        };
    };

    // Create testable WHIP instance
    SrsUniquePtr<TestableWhipForParsing> whip(new TestableWhipForParsing(mock_server.get()));

    // Inject mock config
    whip->config_ = mock_config.get();

    // Create mock response writer
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());

    // Create mock HTTP message for WHIP POST request
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());

    // Set HTTP method to POST
    mock_request->set_method(SRS_CONSTS_HTTP_POST);

    // Set valid SDP offer in request body (simple valid SDP)
    mock_request->body_content_ = "v=0\r\no=- 123456 2 IN IP4 192.168.1.100\r\ns=WebRTC\r\nt=0 0\r\n"
                                  "a=group:BUNDLE 0\r\n"
                                  "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 H264/90000\r\n"
                                  "a=mid:0\r\n";

    // Set query parameters for WHIP publish request (major use scenario)
    mock_request->query_params_["app"] = "live";
    mock_request->query_params_["stream"] = "livestream";
    mock_request->query_params_["eip"] = "203.0.113.10"; // External IP
    mock_request->query_params_["codec"] = "h264";
    mock_request->query_params_["action"] = "publish";                 // Explicit publish action
    mock_request->query_params_["ice-ufrag"] = "testufrag123";         // Valid length (4-32)
    mock_request->query_params_["ice-pwd"] = "testpassword1234567890"; // Valid length (22-32)
    mock_request->query_params_["encrypt"] = "true";                   // SRTP encryption
    mock_request->query_params_["dtls"] = "true";                      // DTLS enabled

    // Set client IP
    mock_request->mock_conn_->remote_ip_ = "192.168.1.100";

    // Create SrsRtcUserConfig object
    SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());

    // Call do_serve_http - major use scenario
    HELPER_EXPECT_SUCCESS(whip->do_serve_http_with(mock_writer.get(), mock_request.get(), ruc.get()));

    // Verify request fields were populated correctly
    EXPECT_STREQ("192.168.1.100", ruc->req_->ip_.c_str());
    EXPECT_STREQ("live", ruc->req_->app_.c_str());
    EXPECT_STREQ("livestream", ruc->req_->stream_.c_str());
    EXPECT_STREQ("testufrag123", ruc->req_->ice_ufrag_.c_str());
    EXPECT_STREQ("testpassword1234567890", ruc->req_->ice_pwd_.c_str());

    // Verify RTC user config fields were set correctly
    EXPECT_STREQ("203.0.113.10", ruc->eip_.c_str());
    EXPECT_STREQ("h264", ruc->vcodec_.c_str());
    EXPECT_TRUE(ruc->publish_); // action=publish
    EXPECT_TRUE(ruc->dtls_);    // dtls=true
    EXPECT_TRUE(ruc->srtp_);    // encrypt=true

    // Verify remote SDP was parsed and stored
    EXPECT_FALSE(ruc->remote_sdp_str_.empty());
    EXPECT_TRUE(ruc->remote_sdp_str_.find("v=0") != std::string::npos);
    EXPECT_TRUE(ruc->remote_sdp_str_.find("a=group:BUNDLE") != std::string::npos);

    // Verify publish handler was called (not play handler)
    EXPECT_TRUE(whip->publish_called_);
    EXPECT_FALSE(whip->play_called_);

    // Clean up
    whip->config_ = NULL;
}

// Test SrsGoApiRtcWhip::serve_http() error handling for invalid SDP.
// This test verifies that WHIP returns HTTP 400 Bad Request when SDP parsing fails.
VOID TEST(SrsGoApiRtcWhipTest, ServeHttpErrorInvalidSdp)
{
    srs_error_t err = srs_success;

    // Create mock RTC API server
    SrsUniquePtr<MockRtcApiServer> mock_server(new MockRtcApiServer());

    // Create testable WHIP handler that simulates SDP parsing error
    class TestableWhip : public SrsGoApiRtcWhip
    {
    public:
        TestableWhip(ISrsRtcApiServer *server) : SrsGoApiRtcWhip(server) {}
        virtual srs_error_t serve_http_with(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
        {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid sdp format");
        }
    };

    // Create testable WHIP instance
    SrsUniquePtr<TestableWhip> whip(new TestableWhip(mock_server.get()));

    // Create mock response writer
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());

    // Create mock HTTP message for WHIP POST request
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());
    mock_request->set_method(SRS_CONSTS_HTTP_POST);

    // Call serve_http - should return HTTP 400 Bad Request
    HELPER_EXPECT_SUCCESS(whip->serve_http(mock_writer.get(), mock_request.get()));

    // Verify response status is 400 Bad Request
    EXPECT_EQ(SRS_CONSTS_HTTP_BadRequest, mock_writer->w->status_);

    // Get the HTTP response
    string response = string(mock_writer->io.out_buffer.bytes(), mock_writer->io.out_buffer.length());
    EXPECT_FALSE(response.empty());

    // Verify the response contains error code and description
    EXPECT_TRUE(response.find("5012") != std::string::npos); // ERROR_RTC_SDP_DECODE
    EXPECT_TRUE(response.find("RtcSdpDecode") != std::string::npos);
}

// Test SrsGoApiRtcWhip::serve_http() error handling for stream busy.
// This test verifies that WHIP returns HTTP 409 Conflict when stream is already publishing.
VOID TEST(SrsGoApiRtcWhipTest, ServeHttpErrorStreamBusy)
{
    srs_error_t err = srs_success;

    // Create mock RTC API server
    SrsUniquePtr<MockRtcApiServer> mock_server(new MockRtcApiServer());

    // Create testable WHIP handler that simulates stream busy error
    class TestableWhip : public SrsGoApiRtcWhip
    {
    public:
        TestableWhip(ISrsRtcApiServer *server) : SrsGoApiRtcWhip(server) {}
        virtual srs_error_t serve_http_with(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
        {
            return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "stream already publishing");
        }
    };

    // Create testable WHIP instance
    SrsUniquePtr<TestableWhip> whip(new TestableWhip(mock_server.get()));

    // Create mock response writer
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());

    // Create mock HTTP message for WHIP POST request
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());
    mock_request->set_method(SRS_CONSTS_HTTP_POST);

    // Call serve_http - should return HTTP 409 Conflict
    HELPER_EXPECT_SUCCESS(whip->serve_http(mock_writer.get(), mock_request.get()));

    // Verify response status is 409 Conflict
    EXPECT_EQ(SRS_CONSTS_HTTP_Conflict, mock_writer->w->status_);

    // Get the HTTP response
    string response = string(mock_writer->io.out_buffer.bytes(), mock_writer->io.out_buffer.length());
    EXPECT_FALSE(response.empty());

    // Verify the response contains error code and description
    EXPECT_TRUE(response.find("1028") != std::string::npos); // ERROR_SYSTEM_STREAM_BUSY
    EXPECT_TRUE(response.find("StreamBusy") != std::string::npos);
}

// Test SrsGoApiRtcWhip::serve_http() error handling for authentication failure.
// This test verifies that WHIP returns HTTP 401 Unauthorized when auth check fails.
VOID TEST(SrsGoApiRtcWhipTest, ServeHttpErrorAuth)
{
    srs_error_t err = srs_success;

    // Create mock RTC API server
    SrsUniquePtr<MockRtcApiServer> mock_server(new MockRtcApiServer());

    // Create testable WHIP handler that simulates auth error
    class TestableWhip : public SrsGoApiRtcWhip
    {
    public:
        TestableWhip(ISrsRtcApiServer *server) : SrsGoApiRtcWhip(server) {}
        virtual srs_error_t serve_http_with(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
        {
            return srs_error_new(ERROR_SYSTEM_AUTH, "authentication failed");
        }
    };

    // Create testable WHIP instance
    SrsUniquePtr<TestableWhip> whip(new TestableWhip(mock_server.get()));

    // Create mock response writer
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());

    // Create mock HTTP message for WHIP POST request
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());
    mock_request->set_method(SRS_CONSTS_HTTP_POST);

    // Call serve_http - should return HTTP 401 Unauthorized
    HELPER_EXPECT_SUCCESS(whip->serve_http(mock_writer.get(), mock_request.get()));

    // Verify response status is 401 Unauthorized
    EXPECT_EQ(SRS_CONSTS_HTTP_Unauthorized, mock_writer->w->status_);

    // Get the HTTP response
    string response = string(mock_writer->io.out_buffer.bytes(), mock_writer->io.out_buffer.length());
    EXPECT_FALSE(response.empty());

    // Verify the response contains error code and description
    EXPECT_TRUE(response.find("1102") != std::string::npos); // ERROR_SYSTEM_AUTH
    EXPECT_TRUE(response.find("SystemAuth") != std::string::npos);
}

// Test SrsGoApiRtcWhip::serve_http() error handling for internal server error.
// This test verifies that WHIP returns HTTP 500 for unexpected errors.
VOID TEST(SrsGoApiRtcWhipTest, ServeHttpErrorInternal)
{
    srs_error_t err = srs_success;

    // Create mock RTC API server
    SrsUniquePtr<MockRtcApiServer> mock_server(new MockRtcApiServer());

    // Create testable WHIP handler that simulates internal error
    class TestableWhip : public SrsGoApiRtcWhip
    {
    public:
        TestableWhip(ISrsRtcApiServer *server) : SrsGoApiRtcWhip(server) {}
        virtual srs_error_t serve_http_with(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
        {
            return srs_error_new(ERROR_SOCKET_TIMEOUT, "socket timeout");
        }
    };

    // Create testable WHIP instance
    SrsUniquePtr<TestableWhip> whip(new TestableWhip(mock_server.get()));

    // Create mock response writer
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());

    // Create mock HTTP message for WHIP POST request
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());
    mock_request->set_method(SRS_CONSTS_HTTP_POST);

    // Call serve_http - should return HTTP 500 Internal Server Error
    HELPER_EXPECT_SUCCESS(whip->serve_http(mock_writer.get(), mock_request.get()));

    // Verify response status is 500 Internal Server Error
    EXPECT_EQ(SRS_CONSTS_HTTP_InternalServerError, mock_writer->w->status_);

    // Get the HTTP response
    string response = string(mock_writer->io.out_buffer.bytes(), mock_writer->io.out_buffer.length());
    EXPECT_FALSE(response.empty());

    // Verify the response contains error code and description
    EXPECT_TRUE(response.find("1011") != std::string::npos); // ERROR_SOCKET_TIMEOUT
    EXPECT_TRUE(response.find("SocketTimeout") != std::string::npos);
}

VOID TEST(RtcApiNackTest, ServeHttpSuccess)
{
    // This test covers the major use scenario for SrsGoApiRtcNACK::serve_http():
    // 1. Client sends GET request to /rtc/v1/nack/ with query parameters:
    //    - username: WebRTC session username (ICE ufrag)
    //    - drop: Number of packets to drop for NACK simulation
    // 2. Server validates the drop parameter (must be > 0)
    // 3. Server finds the RTC session by username (returns NULL if not found)
    // 4. Server returns JSON response with:
    //    - code: Error code (ERROR_RTC_NO_SESSION if session not found)
    //    - query: Echo of query parameters with help text
    //
    // Note: This test verifies the error path where no session is found, which is
    // easier to test than the success path that requires a full SrsRtcConnection mock.
    srs_error_t err = srs_success;

    // Create mock RTC API server (returns NULL for find_rtc_session_by_username)
    SrsUniquePtr<MockRtcApiServer> mock_server(new MockRtcApiServer());

    // Create NACK API handler
    SrsUniquePtr<SrsGoApiRtcNACK> nack_api(new SrsGoApiRtcNACK(mock_server.get()));

    // Create mock response writer
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());

    // Create mock HTTP message with query parameters
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());
    mock_request->query_params_["username"] = "test-user-12345";
    mock_request->query_params_["drop"] = "10";

    // Call serve_http
    // Expected behavior:
    // 1. Parse username and drop from query parameters
    // 2. Validate drop > 0 (passes)
    // 3. Find RTC session by username (returns NULL)
    // 4. Return JSON response with code=ERROR_RTC_NO_SESSION
    HELPER_EXPECT_SUCCESS(nack_api->serve_http(mock_writer.get(), mock_request.get()));

    // Verify the username was used to find the session
    EXPECT_EQ("test-user-12345", mock_server->find_username_);

    // Get the HTTP response
    string response = string(mock_writer->io.out_buffer.bytes(), mock_writer->io.out_buffer.length());
    EXPECT_FALSE(response.empty());

    // Verify the response contains error code for no session (ERROR_RTC_NO_SESSION = 5022)
    EXPECT_TRUE(response.find("\"code\":5022") != std::string::npos);

    // Verify the response contains query echo
    EXPECT_TRUE(response.find("\"username\":\"test-user-12345\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"drop\":\"10\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"help\"") != std::string::npos);
}

// Test SrsGoApiRtcPlay::serve_http() to verify the major use scenario for RTC play.
// This test covers the complete RTC play flow:
// 1. Check remote SDP is valid (BUNDLE policy, has media descriptions, rtcp-mux enabled)
// 2. Configure local SDP with DTLS role and version from config
// 3. Verify RTC server and vhost are enabled (not edge)
// 4. Check if RTC stream is active or RTMP-to-RTC is enabled
// 5. Perform security check
// 6. Call HTTP hooks on_play
// 7. Create RTC session
// 8. Encode local SDP and populate response fields
VOID TEST(GoApiRtcPlayTest, ServeHttpSuccess)
{
    srs_error_t err = srs_success;

    // Create mock RTC API server
    SrsUniquePtr<MockRtcApiServerForPlay> mock_server(new MockRtcApiServerForPlay());

    // Create SrsGoApiRtcPlay instance
    SrsUniquePtr<SrsGoApiRtcPlay> api(new SrsGoApiRtcPlay(mock_server.get()));

    // Create mock config
    SrsUniquePtr<MockAppConfigForRtcPlay> mock_config(new MockAppConfigForRtcPlay());
    mock_config->rtc_server_enabled_ = true;
    mock_config->rtc_enabled_ = true;
    mock_config->vhost_is_edge_ = false;
    mock_config->rtc_from_rtmp_ = false;
    mock_config->http_hooks_enabled_ = false;

    // Create mock RTC source manager
    SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());

    // Create mock live source manager
    SrsUniquePtr<MockLiveSourceManager> mock_live_sources(new MockLiveSourceManager());

    // Create mock HTTP hooks
    SrsUniquePtr<MockHttpHooksForRtcPlay> mock_hooks(new MockHttpHooksForRtcPlay());

    // Create mock security
    SrsUniquePtr<MockSecurity> mock_security(new MockSecurity());

    // Inject mocks into api
    api->config_ = mock_config.get();
    api->rtc_sources_ = mock_rtc_sources.get();
    api->live_sources_ = mock_live_sources.get();
    api->hooks_ = mock_hooks.get();
    api->security_ = mock_security.get();

    // Create RTC user config with valid remote SDP
    SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
    ruc->req_->vhost_ = "__defaultVhost__";
    ruc->req_->app_ = "live";
    ruc->req_->stream_ = "livestream";
    ruc->req_->ip_ = "192.168.1.100";

    // Set up valid remote SDP with BUNDLE policy and rtcp-mux
    std::string remote_sdp_str =
        "v=0\r\n"
        "o=- 123456 2 IN IP4 192.168.1.100\r\n"
        "s=WebRTC\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0 1\r\n"
        "a=msid-semantic: WMS stream\r\n"
        "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp-mux\r\n"
        "a=sendrecv\r\n"
        "a=mid:0\r\n"
        "a=rtpmap:111 opus/48000/2\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp-mux\r\n"
        "a=sendrecv\r\n"
        "a=mid:1\r\n"
        "a=rtpmap:96 H264/90000\r\n";

    ruc->remote_sdp_str_ = remote_sdp_str;
    HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(remote_sdp_str));

    // Create mock response writer and request (not used in this overload but needed for completeness)
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());

    // Test the major use scenario: serve_http() with valid RTC play request
    // This should:
    // 1. Check remote SDP (valid BUNDLE, has media, rtcp-mux enabled)
    // 2. Configure local SDP with DTLS settings
    // 3. Verify RTC is enabled (server and vhost)
    // 4. Check RTC stream status (not active, but that's OK)
    // 5. Perform security check (passes)
    // 6. Call HTTP hooks (none configured)
    // 7. Create RTC session
    // 8. Encode local SDP and populate ruc fields
    HELPER_EXPECT_SUCCESS(api->serve_http(mock_writer.get(), mock_request.get(), ruc.get()));

    // Verify that create_rtc_session was called
    EXPECT_TRUE(mock_server->create_session_called_);

    // Verify that security check was called
    EXPECT_EQ(1, mock_security->check_count_);

    // Verify that local_sdp_str_ was populated (not empty)
    EXPECT_FALSE(ruc->local_sdp_str_.empty());

    // Verify that session_id_ was populated from the mock connection
    EXPECT_STREQ("test-username-12345", ruc->session_id_.c_str());

    // Verify that token_ was populated from the mock connection
    EXPECT_STREQ("test-token-67890", ruc->token_.c_str());

    // Clean up injected dependencies to avoid double-free
    api->config_ = NULL;
    api->rtc_sources_ = NULL;
    api->live_sources_ = NULL;
    api->hooks_ = NULL;
    api->security_ = NULL;
}

// Test SrsGoApiRtcPlay::check_remote_sdp() with valid SDP
// This test covers the major use scenario: validating a proper WebRTC SDP offer
// that contains BUNDLE group policy, audio and video media descriptions with
// rtcp-mux enabled and sendrecv direction (valid for play API).
// Test SrsGoApiRtcPublish::serve_http() - major use scenario for WebRTC publish
// This test covers the major use scenario for RTC publish API: successful publish with valid SDP
VOID TEST(GoApiRtcPublishTest, ServeHttpSuccess)
{
    srs_error_t err = srs_success;

    // Create mock RTC API server
    SrsUniquePtr<MockRtcApiServerForPlay> mock_server(new MockRtcApiServerForPlay());

    // Create SrsGoApiRtcPublish instance
    SrsUniquePtr<SrsGoApiRtcPublish> api(new SrsGoApiRtcPublish(mock_server.get()));

    // Create mock config
    SrsUniquePtr<MockAppConfigForRtcPlay> mock_config(new MockAppConfigForRtcPlay());
    mock_config->rtc_server_enabled_ = true;
    mock_config->rtc_enabled_ = true;
    mock_config->vhost_is_edge_ = false;
    mock_config->dtls_role_ = "passive";
    mock_config->dtls_version_ = "auto";

    // Create mock HTTP hooks
    SrsUniquePtr<MockHttpHooksForRtcPlay> mock_hooks(new MockHttpHooksForRtcPlay());

    // Create mock security
    SrsUniquePtr<MockSecurity> mock_security(new MockSecurity());

    // Create mock statistic
    SrsUniquePtr<MockStatisticForRtcApi> mock_stat(new MockStatisticForRtcApi());
    mock_stat->server_id_ = "test-server-id";
    mock_stat->service_id_ = "test-service-id";
    mock_stat->service_pid_ = "12345";

    // Inject mocks into api
    api->config_ = mock_config.get();
    api->hooks_ = mock_hooks.get();
    api->security_ = mock_security.get();
    api->stat_ = mock_stat.get();

    // Create RTC user config with valid remote SDP
    SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
    ruc->req_->vhost_ = "__defaultVhost__";
    ruc->req_->app_ = "live";
    ruc->req_->stream_ = "livestream";
    ruc->req_->ip_ = "127.0.0.1";
    ruc->publish_ = true;

    // Set up valid remote SDP with BUNDLE and proper media descriptions
    ruc->remote_sdp_.group_policy_ = "BUNDLE";

    // Add video media description
    SrsMediaDesc video_desc("video");
    video_desc.rtcp_mux_ = true;
    video_desc.recvonly_ = false;
    video_desc.sendonly_ = true;
    ruc->remote_sdp_.media_descs_.push_back(video_desc);

    // Add audio media description
    SrsMediaDesc audio_desc("audio");
    audio_desc.rtcp_mux_ = true;
    audio_desc.recvonly_ = false;
    audio_desc.sendonly_ = true;
    ruc->remote_sdp_.media_descs_.push_back(audio_desc);

    ruc->remote_sdp_str_ = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n";

    // Create mock HTTP response writer (not used in this method but required for interface)
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());

    // Create mock HTTP message (not used in this method but required for interface)
    SrsUniquePtr<MockHttpMessageForRtcApi> mock_message(new MockHttpMessageForRtcApi());

    // Test serve_http() - major use scenario: successful publish
    HELPER_EXPECT_SUCCESS(api->serve_http(mock_writer.get(), mock_message.get(), ruc.get()));

    // Verify that create_rtc_session was called
    EXPECT_TRUE(mock_server->create_session_called_);

    // Verify that security check was called
    EXPECT_EQ(1, mock_security->check_count_);

    // Verify that local SDP was generated
    EXPECT_FALSE(ruc->local_sdp_str_.empty());

    // Verify that session ID and token were set
    EXPECT_FALSE(ruc->session_id_.empty());
    EXPECT_FALSE(ruc->token_.empty());

    // Clean up - set to NULL to avoid double-free
    api->config_ = NULL;
    api->hooks_ = NULL;
    api->security_ = NULL;
    api->stat_ = NULL;
}

VOID TEST(RtcApiPlayTest, CheckRemoteSdpSuccess)
{
    srs_error_t err = srs_success;

    // Create mock RTC API server
    SrsUniquePtr<MockRtcApiServerForPlay> mock_server(new MockRtcApiServerForPlay());

    // Create SrsGoApiRtcPlay instance
    SrsUniquePtr<SrsGoApiRtcPlay> api(new SrsGoApiRtcPlay(mock_server.get()));

    // Create a valid WebRTC SDP offer with:
    // - BUNDLE group policy (required)
    // - Audio media description with rtcp-mux and sendrecv
    // - Video media description with rtcp-mux and sendrecv
    std::string remote_sdp_str =
        "v=0\r\n"
        "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0 1\r\n"
        "a=msid-semantic: WMS stream\r\n"
        "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp-mux\r\n"
        "a=sendrecv\r\n"
        "a=mid:0\r\n"
        "a=rtpmap:111 opus/48000/2\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp-mux\r\n"
        "a=sendrecv\r\n"
        "a=mid:1\r\n"
        "a=rtpmap:96 H264/90000\r\n";

    // Parse the SDP
    SrsUniquePtr<SrsSdp> remote_sdp(new SrsSdp());
    HELPER_EXPECT_SUCCESS(remote_sdp->parse(remote_sdp_str));

    // Verify SDP was parsed correctly
    EXPECT_STREQ("BUNDLE", remote_sdp->group_policy_.c_str());
    EXPECT_EQ(2, (int)remote_sdp->media_descs_.size());
    EXPECT_STREQ("audio", remote_sdp->media_descs_[0].type_.c_str());
    EXPECT_TRUE(remote_sdp->media_descs_[0].rtcp_mux_);
    EXPECT_TRUE(remote_sdp->media_descs_[0].sendrecv_);
    EXPECT_FALSE(remote_sdp->media_descs_[0].sendonly_);
    EXPECT_STREQ("video", remote_sdp->media_descs_[1].type_.c_str());
    EXPECT_TRUE(remote_sdp->media_descs_[1].rtcp_mux_);
    EXPECT_TRUE(remote_sdp->media_descs_[1].sendrecv_);
    EXPECT_FALSE(remote_sdp->media_descs_[1].sendonly_);

    // Call check_remote_sdp() - should succeed with valid SDP
    HELPER_EXPECT_SUCCESS(api->check_remote_sdp(*(remote_sdp.get())));
}

VOID TEST(GoApiRtcPublishTest, CheckRemoteSdpSuccess)
{
    srs_error_t err;

    // Create mock server
    SrsUniquePtr<MockRtcApiServerForPlay> mock_server(new MockRtcApiServerForPlay());

    // Create SrsGoApiRtcPublish instance
    SrsUniquePtr<SrsGoApiRtcPublish> api(new SrsGoApiRtcPublish(mock_server.get()));

    // Create a valid WebRTC SDP offer for publishing with:
    // - BUNDLE group policy (required)
    // - Audio media description with rtcp-mux and sendonly (publisher sends audio)
    // - Video media description with rtcp-mux and sendonly (publisher sends video)
    std::string remote_sdp_str =
        "v=0\r\n"
        "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0 1\r\n"
        "a=msid-semantic: WMS stream\r\n"
        "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp-mux\r\n"
        "a=sendonly\r\n"
        "a=mid:0\r\n"
        "a=rtpmap:111 opus/48000/2\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp-mux\r\n"
        "a=sendonly\r\n"
        "a=mid:1\r\n"
        "a=rtpmap:96 H264/90000\r\n";

    // Parse the SDP
    SrsUniquePtr<SrsSdp> remote_sdp(new SrsSdp());
    HELPER_EXPECT_SUCCESS(remote_sdp->parse(remote_sdp_str));

    // Verify SDP was parsed correctly
    EXPECT_STREQ("BUNDLE", remote_sdp->group_policy_.c_str());
    EXPECT_EQ(2, (int)remote_sdp->media_descs_.size());
    EXPECT_STREQ("audio", remote_sdp->media_descs_[0].type_.c_str());
    EXPECT_TRUE(remote_sdp->media_descs_[0].rtcp_mux_);
    EXPECT_TRUE(remote_sdp->media_descs_[0].sendonly_);
    EXPECT_FALSE(remote_sdp->media_descs_[0].recvonly_);
    EXPECT_STREQ("video", remote_sdp->media_descs_[1].type_.c_str());
    EXPECT_TRUE(remote_sdp->media_descs_[1].rtcp_mux_);
    EXPECT_TRUE(remote_sdp->media_descs_[1].sendonly_);
    EXPECT_FALSE(remote_sdp->media_descs_[1].recvonly_);

    // Call check_remote_sdp() - should succeed with valid publish SDP
    HELPER_EXPECT_SUCCESS(api->check_remote_sdp(*(remote_sdp.get())));
}

// Test SrsStatistic find methods: find_vhost_by_id, find_vhost_by_name, find_stream, find_stream_by_url
// This test covers the major use scenario for finding vhosts and streams by different identifiers
VOID TEST(StatisticTest, FindVhostAndStreamByIdAndName)
{
    // Create SrsStatistic object
    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());

    // Create mock request for first vhost and stream
    SrsUniquePtr<MockSrsRequest> req1(new MockSrsRequest("test.vhost1", "live", "stream1"));

    // Create vhost and stream by calling on_stream_publish - major use scenario step 1
    stat->on_stream_publish(req1.get(), "publisher1");

    // Get the created vhost and stream to retrieve their IDs
    SrsStatisticVhost *vhost1 = stat->find_vhost_by_name("test.vhost1");
    EXPECT_TRUE(vhost1 != NULL);
    EXPECT_STREQ("test.vhost1", vhost1->vhost_.c_str());
    std::string vhost1_id = vhost1->id_;

    // Get stream URL and ID
    std::string stream1_url = req1->get_stream_url();
    SrsStatisticStream *stream1 = stat->find_stream_by_url(stream1_url);
    EXPECT_TRUE(stream1 != NULL);
    EXPECT_STREQ("stream1", stream1->stream_.c_str());
    EXPECT_STREQ("live", stream1->app_.c_str());
    std::string stream1_id = stream1->id_;

    // Create mock request for second vhost and stream
    SrsUniquePtr<MockSrsRequest> req2(new MockSrsRequest("test.vhost2", "app2", "stream2"));

    // Create second vhost and stream - major use scenario step 2
    stat->on_stream_publish(req2.get(), "publisher2");

    // Get the created vhost and stream to retrieve their IDs
    SrsStatisticVhost *vhost2 = stat->find_vhost_by_name("test.vhost2");
    EXPECT_TRUE(vhost2 != NULL);
    EXPECT_STREQ("test.vhost2", vhost2->vhost_.c_str());
    std::string vhost2_id = vhost2->id_;

    // Get stream URL and ID
    std::string stream2_url = req2->get_stream_url();
    SrsStatisticStream *stream2 = stat->find_stream_by_url(stream2_url);
    EXPECT_TRUE(stream2 != NULL);
    EXPECT_STREQ("stream2", stream2->stream_.c_str());
    EXPECT_STREQ("app2", stream2->app_.c_str());
    std::string stream2_id = stream2->id_;

    // Test find_vhost_by_id() - major use scenario step 3
    SrsStatisticVhost *found_vhost1 = stat->find_vhost_by_id(vhost1_id);
    EXPECT_TRUE(found_vhost1 != NULL);
    EXPECT_EQ(vhost1, found_vhost1);
    EXPECT_STREQ("test.vhost1", found_vhost1->vhost_.c_str());

    SrsStatisticVhost *found_vhost2 = stat->find_vhost_by_id(vhost2_id);
    EXPECT_TRUE(found_vhost2 != NULL);
    EXPECT_EQ(vhost2, found_vhost2);
    EXPECT_STREQ("test.vhost2", found_vhost2->vhost_.c_str());

    // Test find_vhost_by_id() with non-existent ID - should return NULL
    SrsStatisticVhost *not_found_vhost = stat->find_vhost_by_id("non-existent-id");
    EXPECT_TRUE(not_found_vhost == NULL);

    // Test find_vhost_by_name() - major use scenario step 4
    SrsStatisticVhost *found_vhost_by_name1 = stat->find_vhost_by_name("test.vhost1");
    EXPECT_TRUE(found_vhost_by_name1 != NULL);
    EXPECT_EQ(vhost1, found_vhost_by_name1);
    EXPECT_STREQ(vhost1_id.c_str(), found_vhost_by_name1->id_.c_str());

    SrsStatisticVhost *found_vhost_by_name2 = stat->find_vhost_by_name("test.vhost2");
    EXPECT_TRUE(found_vhost_by_name2 != NULL);
    EXPECT_EQ(vhost2, found_vhost_by_name2);
    EXPECT_STREQ(vhost2_id.c_str(), found_vhost_by_name2->id_.c_str());

    // Test find_vhost_by_name() with non-existent name - should return NULL
    SrsStatisticVhost *not_found_vhost_by_name = stat->find_vhost_by_name("non.existent.vhost");
    EXPECT_TRUE(not_found_vhost_by_name == NULL);

    // Test find_stream() - major use scenario step 5
    SrsStatisticStream *found_stream1 = stat->find_stream(stream1_id);
    EXPECT_TRUE(found_stream1 != NULL);
    EXPECT_EQ(stream1, found_stream1);
    EXPECT_STREQ("stream1", found_stream1->stream_.c_str());
    EXPECT_STREQ("live", found_stream1->app_.c_str());

    SrsStatisticStream *found_stream2 = stat->find_stream(stream2_id);
    EXPECT_TRUE(found_stream2 != NULL);
    EXPECT_EQ(stream2, found_stream2);
    EXPECT_STREQ("stream2", found_stream2->stream_.c_str());
    EXPECT_STREQ("app2", found_stream2->app_.c_str());

    // Test find_stream() with non-existent ID - should return NULL
    SrsStatisticStream *not_found_stream = stat->find_stream("non-existent-stream-id");
    EXPECT_TRUE(not_found_stream == NULL);

    // Test find_stream_by_url() - major use scenario step 6
    SrsStatisticStream *found_stream_by_url1 = stat->find_stream_by_url(stream1_url);
    EXPECT_TRUE(found_stream_by_url1 != NULL);
    EXPECT_EQ(stream1, found_stream_by_url1);
    EXPECT_STREQ(stream1_id.c_str(), found_stream_by_url1->id_.c_str());

    SrsStatisticStream *found_stream_by_url2 = stat->find_stream_by_url(stream2_url);
    EXPECT_TRUE(found_stream_by_url2 != NULL);
    EXPECT_EQ(stream2, found_stream_by_url2);
    EXPECT_STREQ(stream2_id.c_str(), found_stream_by_url2->id_.c_str());

    // Test find_stream_by_url() with non-existent URL - should return NULL
    SrsStatisticStream *not_found_stream_by_url = stat->find_stream_by_url("non/existent/stream");
    EXPECT_TRUE(not_found_stream_by_url == NULL);
}

VOID TEST(StatisticTest, StreamMediaInfo)
{
    srs_error_t err = srs_success;

    // Create SrsStatistic instance
    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());

    // Create mock request for testing
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));

    // Test on_video_info() with AVC codec - major use scenario step 1
    HELPER_EXPECT_SUCCESS(stat->on_video_info(req.get(), SrsVideoCodecIdAVC, SrsAvcProfileHigh, SrsAvcLevel_4, 1920, 1080));

    // Verify video info was set correctly
    SrsStatisticStream *stream = stat->find_stream_by_url(req->get_stream_url());
    EXPECT_TRUE(stream != NULL);
    EXPECT_TRUE(stream->has_video_);
    EXPECT_EQ(SrsVideoCodecIdAVC, stream->vcodec_);
    EXPECT_EQ(SrsAvcProfileHigh, stream->avc_profile_);
    EXPECT_EQ(SrsAvcLevel_4, stream->avc_level_);
    EXPECT_EQ(1920, stream->width_);
    EXPECT_EQ(1080, stream->height_);

    // Test on_video_info() with HEVC codec - major use scenario step 2
    SrsUniquePtr<MockSrsRequest> req2(new MockSrsRequest("test.vhost", "live", "stream2"));
    HELPER_EXPECT_SUCCESS(stat->on_video_info(req2.get(), SrsVideoCodecIdHEVC, SrsHevcProfileMain, SrsHevcLevel_51, 3840, 2160));

    // Verify HEVC video info was set correctly
    SrsStatisticStream *stream2 = stat->find_stream_by_url(req2->get_stream_url());
    EXPECT_TRUE(stream2 != NULL);
    EXPECT_TRUE(stream2->has_video_);
    EXPECT_EQ(SrsVideoCodecIdHEVC, stream2->vcodec_);
    EXPECT_EQ(SrsHevcProfileMain, stream2->hevc_profile_);
    EXPECT_EQ(SrsHevcLevel_51, stream2->hevc_level_);
    EXPECT_EQ(3840, stream2->width_);
    EXPECT_EQ(2160, stream2->height_);

    // Test on_audio_info() - major use scenario step 3
    HELPER_EXPECT_SUCCESS(stat->on_audio_info(req.get(), SrsAudioCodecIdAAC, SrsAudioSampleRate44100, SrsAudioChannelsStereo, SrsAacObjectTypeAacLC));

    // Verify audio info was set correctly
    stream = stat->find_stream_by_url(req->get_stream_url());
    EXPECT_TRUE(stream != NULL);
    EXPECT_TRUE(stream->has_audio_);
    EXPECT_EQ(SrsAudioCodecIdAAC, stream->acodec_);
    EXPECT_EQ(SrsAudioSampleRate44100, stream->asample_rate_);
    EXPECT_EQ(SrsAudioChannelsStereo, stream->asound_type_);
    EXPECT_EQ(SrsAacObjectTypeAacLC, stream->aac_object_);

    // Test on_video_frames() - major use scenario step 4
    HELPER_EXPECT_SUCCESS(stat->on_video_frames(req.get(), 30));
    HELPER_EXPECT_SUCCESS(stat->on_video_frames(req.get(), 25));

    // Verify frame count was accumulated correctly
    stream = stat->find_stream_by_url(req->get_stream_url());
    EXPECT_TRUE(stream != NULL);
    EXPECT_EQ(55, stream->video_frames_->sugar_);
}

// Test SrsStatistic audio sample rate handling for AAC 48000 Hz
// This test verifies the fix for issue #4518 - API should report correct sample rate for AAC streams
VOID TEST(StatisticTest, AudioSampleRateAAC48000Hz)
{
    srs_error_t err = srs_success;

    // Create SrsStatistic instance
    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());

    // Create mock request for testing
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream_48k"));

    // Test on_audio_info() with AAC 48000 Hz sample rate
    HELPER_EXPECT_SUCCESS(stat->on_audio_info(req.get(), SrsAudioCodecIdAAC, SrsAudioSampleRate48000, SrsAudioChannelsStereo, SrsAacObjectTypeAacLC));

    // Verify audio info was set correctly
    SrsStatisticStream *stream = stat->find_stream_by_url(req->get_stream_url());
    EXPECT_TRUE(stream != NULL);
    EXPECT_TRUE(stream->has_audio_);
    EXPECT_EQ(SrsAudioCodecIdAAC, stream->acodec_);
    EXPECT_EQ(SrsAudioSampleRate48000, stream->asample_rate_);
    EXPECT_EQ(SrsAudioChannelsStereo, stream->asound_type_);
    EXPECT_EQ(SrsAacObjectTypeAacLC, stream->aac_object_);

    // Verify JSON dumps reports correct sample rate (48000 Hz, not 44100 Hz)
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(stream->dumps(obj.get()));

    // Check that audio object exists and has correct sample_rate
    SrsJsonAny *audio_any = obj->get_property("audio");
    EXPECT_TRUE(audio_any != NULL);
    EXPECT_TRUE(audio_any->is_object());

    SrsJsonObject *audio = audio_any->to_object();
    SrsJsonAny *sample_rate_any = audio->get_property("sample_rate");
    EXPECT_TRUE(sample_rate_any != NULL);
    EXPECT_EQ(48000, sample_rate_any->to_integer());
}

// Verify that stream statistics expose their age in seconds, matching the
// floating-point "alive" field provided by the clients API.
VOID TEST(StatisticTest, StreamAliveDuration)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream-alive"));

    stat->on_stream_publish(req.get(), "publisher-alive");
    SrsStatisticStream *stream = stat->find_stream_by_url(req->get_stream_url());
    ASSERT_TRUE(stream != NULL);

    // Use the cached clock to make this test deterministic without sleeping.
    stream->create_ = srs_time_now_cached() - 2345 * SRS_UTIME_MILLISECONDS;

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(stream->dumps(obj.get()));

    SrsJsonAny *alive = obj->get_property("alive");
    ASSERT_TRUE(alive != NULL);
    EXPECT_TRUE(alive->is_number());
    EXPECT_DOUBLE_EQ(2.345, alive->to_number());
}

// Test SrsStatistic dumps methods: dumps_streams, dumps_clients, and dumps_hints_kv
// This test covers the major use scenario for dumping statistics to JSON and hints
VOID TEST(StatisticTest, DumpsStreamsClientsAndHints)
{
    srs_error_t err = srs_success;

    // Create SrsStatistic instance
    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());

    // Create multiple streams with different codecs
    SrsUniquePtr<MockSrsRequest> req1(new MockSrsRequest("test.vhost", "live", "stream1"));
    SrsUniquePtr<MockSrsRequest> req2(new MockSrsRequest("test.vhost", "live", "stream2"));
    SrsUniquePtr<MockSrsRequest> req3(new MockSrsRequest("test.vhost", "app2", "stream3"));

    // Register streams by publishing
    stat->on_stream_publish(req1.get(), "publisher-1");
    stat->on_stream_publish(req2.get(), "publisher-2");
    stat->on_stream_publish(req3.get(), "publisher-3");

    // Set video codec info - stream1 with H.264, stream2 with HEVC
    HELPER_EXPECT_SUCCESS(stat->on_video_info(req1.get(), SrsVideoCodecIdAVC, SrsAvcProfileHigh, SrsAvcLevel_31, 1920, 1080));
    HELPER_EXPECT_SUCCESS(stat->on_video_info(req2.get(), SrsVideoCodecIdHEVC, SrsHevcProfileMain, SrsHevcLevel_41, 3840, 2160));
    HELPER_EXPECT_SUCCESS(stat->on_video_info(req3.get(), SrsVideoCodecIdAVC, SrsAvcProfileMain, SrsAvcLevel_3, 1280, 720));

    // Set audio codec info (use valid FLV sample rate indices 0-3)
    HELPER_EXPECT_SUCCESS(stat->on_audio_info(req1.get(), SrsAudioCodecIdAAC, SrsAudioSampleRate44100, SrsAudioChannelsStereo, SrsAacObjectTypeAacLC));
    HELPER_EXPECT_SUCCESS(stat->on_audio_info(req2.get(), SrsAudioCodecIdAAC, SrsAudioSampleRate22050, SrsAudioChannelsStereo, SrsAacObjectTypeAacLC));

    // Register multiple clients for different streams
    MockExpire mock_conn1;
    MockExpire mock_conn2;
    MockExpire mock_conn3;
    MockExpire mock_conn4;

    HELPER_EXPECT_SUCCESS(stat->on_client("client-1", req1.get(), &mock_conn1, SrsRtmpConnPlay));
    HELPER_EXPECT_SUCCESS(stat->on_client("client-2", req1.get(), &mock_conn2, SrsRtmpConnPlay));
    HELPER_EXPECT_SUCCESS(stat->on_client("client-3", req2.get(), &mock_conn3, SrsRtmpConnPlay));
    HELPER_EXPECT_SUCCESS(stat->on_client("client-4", req3.get(), &mock_conn4, SrsRtmpConnFMLEPublish));

    // Add some kbps data to make statistics more realistic
    SrsUniquePtr<MockEphemeralDelta> delta1(new MockEphemeralDelta());
    delta1->add_delta(10240, 20480); // 10KB in, 20KB out
    stat->kbps_add_delta("client-1", delta1.get());

    SrsUniquePtr<MockEphemeralDelta> delta2(new MockEphemeralDelta());
    delta2->add_delta(5120, 15360); // 5KB in, 15KB out
    stat->kbps_add_delta("client-2", delta2.get());

    // Sample kbps to calculate statistics
    stat->kbps_sample();

    // Test dumps_streams() - major use scenario: dump all streams
    SrsUniquePtr<SrsJsonArray> streams_arr(SrsJsonAny::array());
    HELPER_EXPECT_SUCCESS(stat->dumps_streams(streams_arr.get(), 0, 10));

    // Verify streams were dumped correctly
    EXPECT_EQ(3, streams_arr->count());

    // Verify first stream has correct structure
    SrsJsonObject *stream1_obj = streams_arr->at(0)->to_object();
    EXPECT_TRUE(stream1_obj != NULL);
    EXPECT_TRUE(stream1_obj->get_property("id") != NULL);
    EXPECT_TRUE(stream1_obj->get_property("name") != NULL);
    EXPECT_TRUE(stream1_obj->get_property("vhost") != NULL);
    EXPECT_TRUE(stream1_obj->get_property("app") != NULL);

    // Test dumps_streams() with pagination - start=1, count=2
    SrsUniquePtr<SrsJsonArray> streams_arr_page(SrsJsonAny::array());
    HELPER_EXPECT_SUCCESS(stat->dumps_streams(streams_arr_page.get(), 1, 2));

    // Should skip first stream and return next 2 streams
    EXPECT_EQ(2, streams_arr_page->count());

    // Test dumps_clients() - major use scenario: dump all clients
    SrsUniquePtr<SrsJsonArray> clients_arr(SrsJsonAny::array());
    HELPER_EXPECT_SUCCESS(stat->dumps_clients(clients_arr.get(), 0, 10));

    // Verify clients were dumped correctly
    EXPECT_EQ(4, clients_arr->count());

    // Verify first client has correct structure
    SrsJsonObject *client1_obj = clients_arr->at(0)->to_object();
    EXPECT_TRUE(client1_obj != NULL);
    EXPECT_TRUE(client1_obj->get_property("id") != NULL);
    EXPECT_TRUE(client1_obj->get_property("type") != NULL);
    EXPECT_TRUE(client1_obj->get_property("alive") != NULL);

    // Test dumps_clients() with pagination - start=2, count=2
    SrsUniquePtr<SrsJsonArray> clients_arr_page(SrsJsonAny::array());
    HELPER_EXPECT_SUCCESS(stat->dumps_clients(clients_arr_page.get(), 2, 2));

    // Should skip first 2 clients and return next 2 clients
    EXPECT_EQ(2, clients_arr_page->count());

    // Test dumps_hints_kv() - major use scenario: generate hints string
    std::stringstream ss;
    stat->dumps_hints_kv(ss);
    std::string hints = ss.str();

    // Verify hints contain expected information
    EXPECT_TRUE(hints.find("&streams=3") != std::string::npos);
    EXPECT_TRUE(hints.find("&clients=4") != std::string::npos);

    // Verify HEVC hint is present (stream2 has HEVC codec)
    EXPECT_TRUE(hints.find("&h265=1") != std::string::npos);

    // Verify kbps hints - they may or may not be present depending on whether kbps values are non-zero
    // The hints string should at least contain streams and clients info
    EXPECT_TRUE(hints.length() > 0);
}

VOID TEST(StatisticTest, KbpsAddDelta)
{
    srs_error_t err = srs_success;

    // Create SrsStatistic instance
    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());

    // Create mock request and register a client
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    MockExpire mock_conn;
    std::string client_id = "client-123";
    HELPER_EXPECT_SUCCESS(stat->on_client(client_id, req.get(), &mock_conn, SrsRtmpConnPlay));

    // Create mock delta with test data
    SrsUniquePtr<MockEphemeralDelta> delta(new MockEphemeralDelta());
    delta->add_delta(1024, 2048); // 1KB in, 2KB out

    // Get initial kbps values (should be 0)
    SrsStatisticClient *client = stat->clients_[client_id];
    EXPECT_TRUE(client != NULL);
    int64_t initial_server_recv = stat->kbps_->get_recv_bytes();
    int64_t initial_server_send = stat->kbps_->get_send_bytes();
    int64_t initial_client_recv = client->kbps_->get_recv_bytes();
    int64_t initial_client_send = client->kbps_->get_send_bytes();
    int64_t initial_stream_recv = client->stream_->kbps_->get_recv_bytes();
    int64_t initial_stream_send = client->stream_->kbps_->get_send_bytes();
    int64_t initial_vhost_recv = client->stream_->vhost_->kbps_->get_recv_bytes();
    int64_t initial_vhost_send = client->stream_->vhost_->kbps_->get_send_bytes();

    // Call kbps_add_delta() - major use scenario
    stat->kbps_add_delta(client_id, delta.get());

    // Verify delta was added to all levels (server, client, stream, vhost)
    EXPECT_EQ(initial_server_recv + 1024, stat->kbps_->get_recv_bytes());
    EXPECT_EQ(initial_server_send + 2048, stat->kbps_->get_send_bytes());
    EXPECT_EQ(initial_client_recv + 1024, client->kbps_->get_recv_bytes());
    EXPECT_EQ(initial_client_send + 2048, client->kbps_->get_send_bytes());
    EXPECT_EQ(initial_stream_recv + 1024, client->stream_->kbps_->get_recv_bytes());
    EXPECT_EQ(initial_stream_send + 2048, client->stream_->kbps_->get_send_bytes());
    EXPECT_EQ(initial_vhost_recv + 1024, client->stream_->vhost_->kbps_->get_recv_bytes());
    EXPECT_EQ(initial_vhost_send + 2048, client->stream_->vhost_->kbps_->get_send_bytes());

    // Verify delta was consumed (remark() resets the delta)
    int64_t remaining_in = 0, remaining_out = 0;
    delta->remark(&remaining_in, &remaining_out);
    EXPECT_EQ(0, remaining_in);
    EXPECT_EQ(0, remaining_out);
}

// Test SrsStatistic::dumps_metrics() - major use scenario for exporting metrics
// This test covers the major use scenario for dumping server metrics
VOID TEST(StatisticTest, DumpsMetrics)
{
    srs_error_t err;

    // Create SrsStatistic object
    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());

    // Create mock requests for streams and clients
    SrsUniquePtr<MockSrsRequest> req1(new MockSrsRequest("test.vhost", "live", "stream1"));
    SrsUniquePtr<MockSrsRequest> req2(new MockSrsRequest("test.vhost", "live", "stream2"));
    SrsUniquePtr<MockSrsRequest> req3(new MockSrsRequest("test.vhost", "app", "stream3"));

    // Simulate stream publishing to populate streams_
    stat->on_stream_publish(req1.get(), "publisher1");
    stat->on_stream_publish(req2.get(), "publisher2");
    stat->on_stream_publish(req3.get(), "publisher3");

    // Simulate client connections to populate clients_
    MockExpire mock_conn1;
    MockExpire mock_conn2;
    MockExpire mock_conn3;
    MockExpire mock_conn4;
    MockExpire mock_conn5;

    HELPER_EXPECT_SUCCESS(stat->on_client("client1", req1.get(), &mock_conn1, SrsRtmpConnPlay));
    HELPER_EXPECT_SUCCESS(stat->on_client("client2", req1.get(), &mock_conn2, SrsRtmpConnPlay));
    HELPER_EXPECT_SUCCESS(stat->on_client("client3", req2.get(), &mock_conn3, SrsRtmpConnPlay));
    HELPER_EXPECT_SUCCESS(stat->on_client("client4", req3.get(), &mock_conn4, SrsRtmpConnFMLEPublish));
    HELPER_EXPECT_SUCCESS(stat->on_client("client5", req3.get(), &mock_conn5, SrsRtmpConnPlay));

    // Simulate some bytes sent/received by adding delta to kbps
    stat->kbps_add_delta("client1", NULL);
    stat->kbps_add_delta("client2", NULL);
    stat->kbps_sample();

    // Manually set some bytes in kbps for testing
    // Note: We need to add delta to kbps to simulate traffic
    stat->kbps_->add_delta(1024 * 100, 1024 * 200); // 100KB recv, 200KB send
    stat->kbps_->sample();

    // Simulate some client disconnections with genuine errors to increment nb_errs_.
    err = srs_error_new(ERROR_SYSTEM_CLIENT_INVALID, "test error 1");
    stat->on_disconnect("client1", err);
    srs_freep(err);
    err = srs_error_new(ERROR_RTMP_HANDSHAKE, "test error 2");
    stat->on_disconnect("client2", err);
    srs_freep(err);

    // Test dumps_metrics() - major use scenario
    int64_t send_bytes = 0;
    int64_t recv_bytes = 0;
    int64_t nstreams = 0;
    int64_t nclients = 0;
    int64_t total_nclients = 0;
    int64_t nerrs = 0;

    HELPER_EXPECT_SUCCESS(stat->dumps_metrics(send_bytes, recv_bytes, nstreams, nclients, total_nclients, nerrs));

    // Verify metrics are correctly dumped
    // send_bytes and recv_bytes should be from kbps_->get_send_bytes() and kbps_->get_recv_bytes()
    EXPECT_EQ(1024 * 200, send_bytes);
    EXPECT_EQ(1024 * 100, recv_bytes);

    // nstreams should be 3 (stream1, stream2, stream3)
    EXPECT_EQ(3, nstreams);

    // nclients should be 3 (client3, client4, client5 - client1 and client2 disconnected)
    EXPECT_EQ(3, nclients);

    // total_nclients should be 5 (all clients that ever connected)
    EXPECT_EQ(5, total_nclients);

    // nerrs should be 2 (client1 and client2 disconnected with errors)
    EXPECT_EQ(2, nerrs);
}

MockAppConfigForStatistic::MockAppConfigForStatistic()
{
    get_server_id_count_ = 0;
    vhost_enabled_ = false;
    hls_enabled_ = false;
    hls_fragment_ = 0;
}

MockAppConfigForStatistic::~MockAppConfigForStatistic()
{
}

std::string MockAppConfigForStatistic::get_server_id()
{
    get_server_id_count_++;
    return server_id_;
}

bool MockAppConfigForStatistic::get_vhost_enabled(std::string vhost)
{
    vhost_calls_.push_back("vhost_enabled:" + vhost);
    return vhost_enabled_;
}

bool MockAppConfigForStatistic::get_hls_enabled(std::string vhost)
{
    vhost_calls_.push_back("hls_enabled:" + vhost);
    return hls_enabled_;
}

srs_utime_t MockAppConfigForStatistic::get_hls_fragment(std::string vhost)
{
    vhost_calls_.push_back("hls_fragment:" + vhost);
    return hls_fragment_;
}

// The statistic captures the config global in its constructor.
VOID TEST(StatisticTest, CapturesConfigInConstructor)
{
    SrsStatistic stat;
    EXPECT_TRUE(stat.config_ == (ISrsAppConfig *)_srs_config);
}

// The server id comes from the injected config, read once and then cached.
VOID TEST(StatisticTest, ServerIdFromInjectedConfig)
{
    MockAppConfigForStatistic config;
    config.server_id_ = "vid-injected";

    SrsStatistic stat;
    stat.config_ = &config;

    EXPECT_STREQ("vid-injected", stat.server_id().c_str());
    EXPECT_STREQ("vid-injected", stat.server_id().c_str());
    EXPECT_EQ(1, config.get_server_id_count_);

    stat.config_ = NULL;
}

// The constructor allocates the generator that draws the service id, so a test can replace it
// before service_id() is called.
VOID TEST(StatisticTest, ConstructorCreatesRand)
{
    SrsStatistic stat;
    EXPECT_TRUE(stat.rand_ != NULL);
    EXPECT_TRUE(stat.service_id_.empty());
}

// The service id is an 8-character string drawn once through the injected generator, then cached.
VOID TEST(StatisticTest, ServiceIdDrawsThroughInjectedRand)
{
    MockRandForHandshake rand;
    rand.gen_str_values_.push_back("service1");
    rand.gen_str_values_.push_back("service2");

    SrsStatistic stat;
    srs_freep(stat.rand_);
    stat.rand_ = &rand;

    std::string first_id = stat.service_id();
    std::string second_id = stat.service_id();

    // Restore the stack member before any assertion, so the statistic never frees it.
    stat.rand_ = NULL;

    EXPECT_EQ(1, (int)rand.gen_str_lens_.size());
    if (rand.gen_str_lens_.size() == 1) {
        EXPECT_EQ(8, rand.gen_str_lens_[0]);
    }
    EXPECT_STREQ("service1", first_id.c_str());
    EXPECT_STREQ("service1", second_id.c_str());
}

// The vhost statistic captures the config global in its constructor.
VOID TEST(StatisticTest, VhostCapturesConfigInConstructor)
{
    SrsStatisticVhost vhost;
    EXPECT_TRUE(vhost.config_ == (ISrsAppConfig *)_srs_config);
}

// The vhost and HLS switches and the HLS fragment come from the injected config.
VOID TEST(StatisticTest, VhostDumpsFromInjectedConfig)
{
    srs_error_t err = srs_success;

    MockAppConfigForStatistic config;
    config.vhost_enabled_ = false;
    config.hls_enabled_ = true;
    config.hls_fragment_ = 2500 * SRS_UTIME_MILLISECONDS;

    SrsStatisticVhost vhost;
    vhost.config_ = &config;
    vhost.vhost_ = "test.vhost";

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(vhost.dumps(obj.get()));

    ASSERT_EQ(3, (int)config.vhost_calls_.size());
    EXPECT_STREQ("hls_enabled:test.vhost", config.vhost_calls_[0].c_str());
    EXPECT_STREQ("vhost_enabled:test.vhost", config.vhost_calls_[1].c_str());
    EXPECT_STREQ("hls_fragment:test.vhost", config.vhost_calls_[2].c_str());

    SrsJsonAny *enabled = obj->get_property("enabled");
    ASSERT_TRUE(enabled && enabled->is_boolean());
    EXPECT_FALSE(enabled->to_boolean());

    SrsJsonAny *hls = obj->get_property("hls");
    ASSERT_TRUE(hls && hls->is_object());
    SrsJsonAny *hls_enabled = hls->to_object()->get_property("enabled");
    ASSERT_TRUE(hls_enabled && hls_enabled->is_boolean());
    EXPECT_TRUE(hls_enabled->to_boolean());
    SrsJsonAny *fragment = hls->to_object()->get_property("fragment");
    ASSERT_TRUE(fragment && fragment->is_number());
    EXPECT_DOUBLE_EQ(2.5, fragment->to_number());

    vhost.config_ = NULL;
}

// With HLS disabled, the fragment is neither read nor dumped.
VOID TEST(StatisticTest, VhostDumpsWithoutHlsFragment)
{
    srs_error_t err = srs_success;

    MockAppConfigForStatistic config;
    config.vhost_enabled_ = true;
    config.hls_enabled_ = false;

    SrsStatisticVhost vhost;
    vhost.config_ = &config;
    vhost.vhost_ = "test.vhost";

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(vhost.dumps(obj.get()));

    ASSERT_EQ(2, (int)config.vhost_calls_.size());
    EXPECT_STREQ("hls_enabled:test.vhost", config.vhost_calls_[0].c_str());
    EXPECT_STREQ("vhost_enabled:test.vhost", config.vhost_calls_[1].c_str());

    SrsJsonAny *enabled = obj->get_property("enabled");
    ASSERT_TRUE(enabled && enabled->is_boolean());
    EXPECT_TRUE(enabled->to_boolean());

    SrsJsonAny *hls = obj->get_property("hls");
    ASSERT_TRUE(hls && hls->is_object());
    SrsJsonAny *hls_enabled = hls->to_object()->get_property("enabled");
    ASSERT_TRUE(hls_enabled && hls_enabled->is_boolean());
    EXPECT_FALSE(hls_enabled->to_boolean());
    EXPECT_TRUE(hls->to_object()->get_property("fragment") == NULL);

    vhost.config_ = NULL;
}

// The vhost statistic allocates its random generator but generates no id until assemble(),
// so a test can inject its own generator first.
VOID TEST(StatisticTest, VhostCreatesRandWithoutGeneratingId)
{
    SrsStatisticVhost vhost;
    EXPECT_TRUE(vhost.rand_ != NULL);
    EXPECT_TRUE(vhost.id_.empty());
}

// The vhost id is generated by the injected random generator, with the constant prefix.
VOID TEST(StatisticTest, VhostAssembleGeneratesIdThroughRand)
{
    MockRandForHandshake rand;

    SrsStatisticVhost vhost;
    srs_freep(vhost.rand_);
    vhost.rand_ = &rand;

    vhost.assemble();

    ASSERT_EQ(1, (int)rand.gen_str_lens_.size());
    EXPECT_EQ(7, rand.gen_str_lens_[0]);
    EXPECT_STREQ("vid-xxxxxxx", vhost.id_.c_str());

    vhost.rand_ = NULL;
}

// The statistic assembles every vhost it creates, so a new vhost has a generated id and is
// found by it.
VOID TEST(StatisticTest, CreateVhostCreatesAssembledVhost)
{
    SrsStatistic stat;
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));

    SrsStatisticVhost *vhost = stat.create_vhost(req.get());
    ASSERT_TRUE(vhost != NULL);
    EXPECT_EQ(11, (int)vhost->id_.size());
    EXPECT_EQ(0, (int)vhost->id_.find("vid-"));
    EXPECT_TRUE(stat.find_vhost_by_id(vhost->id_) == vhost);
}

// The client statistic only captures the clock and reads no time, so a test can inject
// its own clock before assemble() stamps the creation time.
VOID TEST(StatisticTest, ClientCapturesClockAndReadsNoTime)
{
    SrsStatisticClient client;
    EXPECT_TRUE(client.clk_ != NULL);
    EXPECT_TRUE(client.clk_ == _srs_clock);
    EXPECT_EQ((srs_utime_t)0, client.create_);
}

// assemble() stamps the creation time with the injected clock.
VOID TEST(StatisticTest, ClientAssembleStampsCreateWithInjectedClock)
{
    MockClockForPithyPrint clock;
    clock.now_ = 100 * SRS_UTIME_SECONDS;

    SrsStatisticClient client;
    client.clk_ = &clock;
    client.assemble();

    EXPECT_EQ(100 * SRS_UTIME_SECONDS, client.create_);

    client.clk_ = NULL;
}

// The alive duration in dumps() is measured on the injected clock.
VOID TEST(StatisticTest, ClientDumpsAliveFromInjectedClock)
{
    srs_error_t err = srs_success;

    MockClockForPithyPrint clock;
    clock.now_ = 100 * SRS_UTIME_SECONDS;

    SrsStatisticVhost vhost;
    vhost.id_ = "vid-vhost";
    SrsStatisticStream stream;
    stream.assemble();
    stream.id_ = "vid-stream";
    stream.vhost_ = &vhost;

    SrsStatisticClient client;
    client.clk_ = &clock;
    client.assemble();
    client.id_ = "client-1";
    client.stream_ = &stream;
    client.req_ = new MockSrsRequest("test.vhost", "live", "stream1");
    client.type_ = SrsRtmpConnPlay;

    clock.now_ = 102500 * SRS_UTIME_MILLISECONDS;

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(client.dumps(obj.get()));

    SrsJsonAny *alive = obj->get_property("alive");
    ASSERT_TRUE(alive && alive->is_number());
    EXPECT_DOUBLE_EQ(2.5, alive->to_number());

    client.clk_ = NULL;
}

// The statistic assembles every client it creates, so a new client has the global clock
// and its creation time.
VOID TEST(StatisticTest, OnClientCreatesAssembledClient)
{
    srs_error_t err = srs_success;

    SrsStatistic stat;
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    HELPER_EXPECT_SUCCESS(stat.on_client("client-1", req.get(), NULL, SrsRtmpConnPlay));

    SrsStatisticClient *client = stat.find_client("client-1");
    ASSERT_TRUE(client != NULL);
    EXPECT_TRUE(client->clk_ == _srs_clock);
    EXPECT_TRUE(client->create_ > 0);
}

// The stream statistic only captures the clock and reads no time, so a test can inject
// its own clock before assemble() stamps the creation time.
VOID TEST(StatisticTest, StreamCapturesClockAndReadsNoTime)
{
    SrsStatisticStream stream;
    EXPECT_TRUE(stream.clk_ != NULL);
    EXPECT_TRUE(stream.clk_ == _srs_clock);
    EXPECT_EQ((srs_utime_t)0, stream.create_);
}

// assemble() stamps the creation time with the injected clock.
VOID TEST(StatisticTest, StreamAssembleStampsCreateWithInjectedClock)
{
    MockClockForPithyPrint clock;
    clock.now_ = 100 * SRS_UTIME_SECONDS;

    SrsStatisticStream stream;
    stream.clk_ = &clock;
    stream.assemble();

    EXPECT_EQ(100 * SRS_UTIME_SECONDS, stream.create_);

    stream.clk_ = NULL;
}

// The live time and the alive duration in dumps() are measured on the injected clock.
VOID TEST(StatisticTest, StreamDumpsLiveAndAliveFromInjectedClock)
{
    srs_error_t err = srs_success;

    MockClockForPithyPrint clock;
    clock.now_ = 100 * SRS_UTIME_SECONDS;

    SrsStatisticVhost vhost;
    vhost.id_ = "vid-vhost";

    SrsStatisticStream stream;
    stream.clk_ = &clock;
    stream.assemble();
    stream.id_ = "vid-stream";
    stream.vhost_ = &vhost;

    clock.now_ = 102500 * SRS_UTIME_MILLISECONDS;

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(stream.dumps(obj.get()));

    SrsJsonAny *live_ms = obj->get_property("live_ms");
    ASSERT_TRUE(live_ms && live_ms->is_integer());
    EXPECT_EQ(102500, live_ms->to_integer());

    SrsJsonAny *alive = obj->get_property("alive");
    ASSERT_TRUE(alive && alive->is_number());
    EXPECT_DOUBLE_EQ(2.5, alive->to_number());

    stream.clk_ = NULL;
}

// The statistic assembles every stream it creates, so a new stream has the global clock
// and its creation time.
VOID TEST(StatisticTest, OnClientCreatesAssembledStream)
{
    srs_error_t err = srs_success;

    SrsStatistic stat;
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    HELPER_EXPECT_SUCCESS(stat.on_client("client-1", req.get(), NULL, SrsRtmpConnPlay));

    SrsStatisticClient *client = stat.find_client("client-1");
    ASSERT_TRUE(client != NULL);
    ASSERT_TRUE(client->stream_ != NULL);
    EXPECT_TRUE(client->stream_->clk_ == _srs_clock);
    EXPECT_TRUE(client->stream_->create_ > 0);
}

// The stream statistic allocates its random generator but generates no id until assemble(),
// so a test can inject its own generator first.
VOID TEST(StatisticTest, StreamCreatesRandWithoutGeneratingId)
{
    SrsStatisticStream stream;
    EXPECT_TRUE(stream.rand_ != NULL);
    EXPECT_TRUE(stream.id_.empty());
}

// The stream id is generated by the injected random generator, with the constant prefix.
VOID TEST(StatisticTest, StreamAssembleGeneratesIdThroughRand)
{
    MockRandForHandshake rand;

    SrsStatisticStream stream;
    srs_freep(stream.rand_);
    stream.rand_ = &rand;

    stream.assemble();

    ASSERT_EQ(1, (int)rand.gen_str_lens_.size());
    EXPECT_EQ(7, rand.gen_str_lens_[0]);
    EXPECT_STREQ("vid-xxxxxxx", stream.id_.c_str());

    stream.rand_ = NULL;
}

// The statistic assembles every stream it creates, so a new stream has a generated id and is
// found by it.
VOID TEST(StatisticTest, CreateStreamCreatesStreamFoundById)
{
    SrsStatistic stat;
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));

    SrsStatisticVhost *vhost = stat.create_vhost(req.get());
    SrsStatisticStream *stream = stat.create_stream(vhost, req.get());
    ASSERT_TRUE(stream != NULL);
    EXPECT_EQ(11, (int)stream->id_.size());
    EXPECT_EQ(0, (int)stream->id_.find("vid-"));
    EXPECT_TRUE(stat.find_stream(stream->id_) == stream);
}

MockAppConfigForCoWorkers::MockAppConfigForCoWorkers()
{
    get_listens_count_ = 0;
    get_http_api_listens_count_ = 0;
}

MockAppConfigForCoWorkers::~MockAppConfigForCoWorkers()
{
}

std::vector<std::string> MockAppConfigForCoWorkers::get_listens()
{
    get_listens_count_++;
    return listens_;
}

std::vector<std::string> MockAppConfigForCoWorkers::get_http_api_listens()
{
    get_http_api_listens_count_++;
    return http_api_listens_;
}

SrsConfDirective *MockAppConfigForCoWorkers::get_vhost(std::string vhost, bool try_default_vhost)
{
    get_vhost_calls_.push_back(vhost);
    return default_vhost_;
}

// The coworkers capture the config global in the constructor.
VOID TEST(CoWorkersTest, CapturesConfigInConstructor)
{
    SrsCoWorkers workers;
    EXPECT_TRUE(workers.config_ != NULL);
    EXPECT_TRUE(workers.config_ == (ISrsAppConfig *)_srs_config);
}

// The service ip, port, vhost and API endpoint come from the injected config.
VOID TEST(CoWorkersTest, DumpsFromInjectedConfig)
{
    srs_error_t err = srs_success;

    MockAppConfigForCoWorkers config;
    config.listens_.push_back("192.168.1.5:19350");
    config.http_api_listens_.push_back("11985");
    config.default_vhost_ = new SrsConfDirective();
    config.default_vhost_->name_ = "vhost";
    config.default_vhost_->args_.push_back("__defaultVhost__");

    SrsCoWorkers workers;
    workers.config_ = &config;

    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    HELPER_EXPECT_SUCCESS(workers.on_publish(&req));

    SrsUniquePtr<SrsJsonAny> data(workers.dumps("__defaultVhost__", "10.0.0.1:1985", "live", "livestream"));
    ASSERT_TRUE(data->is_object());
    SrsJsonObject *obj = data->to_object();
    EXPECT_STREQ("192.168.1.5", obj->get_property("ip")->to_str().c_str());
    EXPECT_EQ(19350, obj->get_property("port")->to_integer());
    EXPECT_STREQ("__defaultVhost__", obj->get_property("vhost")->to_str().c_str());
    EXPECT_STREQ("192.168.1.5:11985", obj->get_property("api")->to_str().c_str());

    ASSERT_EQ(1, (int)config.get_vhost_calls_.size());
    EXPECT_STREQ("__defaultVhost__", config.get_vhost_calls_.at(0).c_str());
    EXPECT_EQ(1, config.get_listens_count_);
    EXPECT_EQ(1, config.get_http_api_listens_count_);

    workers.config_ = NULL;
}

// A port-only listen takes the service ip from the coworker host, and a stream
// published on the default vhost is found through the vhost the injected config resolves.
VOID TEST(CoWorkersTest, DumpsCoworkerHostForPortOnlyListen)
{
    srs_error_t err = srs_success;

    MockAppConfigForCoWorkers config;
    config.listens_.push_back("19350");
    config.http_api_listens_.push_back("127.0.0.1:11985");
    config.default_vhost_ = new SrsConfDirective();
    config.default_vhost_->name_ = "vhost";
    config.default_vhost_->args_.push_back("__defaultVhost__");

    SrsCoWorkers workers;
    workers.config_ = &config;

    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    HELPER_EXPECT_SUCCESS(workers.on_publish(&req));

    SrsUniquePtr<SrsJsonAny> data(workers.dumps("unknown.vhost", "10.0.0.1:1985", "live", "livestream"));
    ASSERT_TRUE(data->is_object());
    SrsJsonObject *obj = data->to_object();
    EXPECT_STREQ("10.0.0.1", obj->get_property("ip")->to_str().c_str());
    EXPECT_EQ(19350, obj->get_property("port")->to_integer());
    EXPECT_STREQ("127.0.0.1:11985", obj->get_property("api")->to_str().c_str());

    ASSERT_EQ(1, (int)config.get_vhost_calls_.size());
    EXPECT_STREQ("unknown.vhost", config.get_vhost_calls_.at(0).c_str());

    workers.config_ = NULL;
}

// When the injected config has no such vhost, dumps() returns null without reading the listens.
VOID TEST(CoWorkersTest, DumpsNullWhenInjectedConfigHasNoVhost)
{
    srs_error_t err = srs_success;

    MockAppConfigForCoWorkers config;
    config.listens_.push_back("19350");
    config.http_api_listens_.push_back("11985");

    SrsCoWorkers workers;
    workers.config_ = &config;

    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    HELPER_EXPECT_SUCCESS(workers.on_publish(&req));

    SrsUniquePtr<SrsJsonAny> data(workers.dumps("__defaultVhost__", "10.0.0.1:1985", "live", "livestream"));
    EXPECT_TRUE(data->is_null());

    EXPECT_EQ(1, (int)config.get_vhost_calls_.size());
    EXPECT_EQ(0, config.get_listens_count_);
    EXPECT_EQ(0, config.get_http_api_listens_count_);

    workers.config_ = NULL;
}

MockCoWorkersForGoApiClusters::MockCoWorkersForGoApiClusters()
{
    dumps_count_ = 0;
}

MockCoWorkersForGoApiClusters::~MockCoWorkersForGoApiClusters()
{
}

SrsJsonAny *MockCoWorkersForGoApiClusters::dumps(std::string vhost, std::string coworker, std::string app, std::string stream)
{
    dumps_count_++;
    dumps_vhost_ = vhost;
    dumps_coworker_ = coworker;
    dumps_app_ = app;
    dumps_stream_ = stream;
    return SrsJsonAny::object()->set("ip", SrsJsonAny::str("10.0.0.9"));
}

srs_error_t MockCoWorkersForGoApiClusters::on_publish(ISrsRequest *r)
{
    return srs_success;
}

void MockCoWorkersForGoApiClusters::on_unpublish(ISrsRequest *r)
{
}

// The global coworkers is the singleton, set by the global initialize before any API handler exists.
VOID TEST(GoApiClustersTest, GlobalInitializeSetsCoWorkers)
{
    EXPECT_TRUE(_srs_coworkers != NULL);
    EXPECT_TRUE(_srs_coworkers == SrsCoWorkers::instance());
}

// The clusters API captures the coworkers global in the constructor.
VOID TEST(GoApiClustersTest, CapturesCoWorkersInConstructor)
{
    SrsGoApiClusters api;
    EXPECT_TRUE(api.coworkers_ != NULL);
    EXPECT_TRUE(api.coworkers_ == (ISrsCoWorkers *)_srs_coworkers);
}

// The origin in the response is dumped by the injected coworkers, with the vhost, coworker, app and stream of the query.
VOID TEST(GoApiClustersTest, ServeHttpDumpsThroughInjectedCoWorkers)
{
    srs_error_t err = srs_success;

    MockCoWorkersForGoApiClusters coworkers;
    SrsGoApiClusters api;
    api.coworkers_ = &coworkers;

    SrsUniquePtr<SrsHttpMessage> req(new SrsHttpMessage());
    HELPER_EXPECT_SUCCESS(req->set_url("http://127.0.0.1/api/v1/clusters?ip=192.168.1.100&vhost=test.vhost&app=live&stream=livestream&coworker=127.0.0.1:1985", false));

    MockResponseWriter w;
    HELPER_EXPECT_SUCCESS(api.serve_http(&w, req.get()));

    EXPECT_EQ(1, coworkers.dumps_count_);
    EXPECT_STREQ("test.vhost", coworkers.dumps_vhost_.c_str());
    EXPECT_STREQ("127.0.0.1:1985", coworkers.dumps_coworker_.c_str());
    EXPECT_STREQ("live", coworkers.dumps_app_.c_str());
    EXPECT_STREQ("livestream", coworkers.dumps_stream_.c_str());

    string response = HELPER_BUFFER2STR(&w.io.out_buffer);
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"origin\":{\"ip\":\"10.0.0.9\"}") != string::npos);
    EXPECT_TRUE(response.find("\"vhost\":\"test.vhost\"") != string::npos);

    api.coworkers_ = NULL;
}

MockAppConfigForSummaries::MockAppConfigForSummaries()
{
    argv_count_ = 0;
    cwd_count_ = 0;
}

MockAppConfigForSummaries::~MockAppConfigForSummaries()
{
}

std::string MockAppConfigForSummaries::argv()
{
    argv_count_++;
    return argv_;
}

std::string MockAppConfigForSummaries::cwd()
{
    cwd_count_++;
    return cwd_;
}

// Return data.self of a summaries object, or NULL.
static SrsJsonObject *find_summaries_self(SrsJsonObject *obj)
{
    SrsJsonAny *data = obj->get_property("data");
    if (!data || !data->is_object()) {
        return NULL;
    }

    SrsJsonAny *self = data->to_object()->get_property("self");
    if (!self || !self->is_object()) {
        return NULL;
    }

    return self->to_object();
}

// The command line and work directory in the summaries come from the given config.
VOID TEST(ApiSummariesTest, DumpsArgvAndCwdFromGivenConfig)
{
    MockAppConfigForSummaries config;
    config.argv_ = "./objs/srs -c conf/summaries.conf";
    config.cwd_ = "/tmp/srs-summaries";

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    srs_api_dump_summaries(&config, obj.get());

    SrsJsonObject *self = find_summaries_self(obj.get());
    ASSERT_TRUE(self != NULL);

    SrsJsonAny *prop = self->get_property("argv");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("./objs/srs -c conf/summaries.conf", prop->to_str().c_str());

    prop = self->get_property("cwd");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("/tmp/srs-summaries", prop->to_str().c_str());

    EXPECT_EQ(1, config.argv_count_);
    EXPECT_EQ(1, config.cwd_count_);
}

// The summaries API captures the config global in the constructor.
VOID TEST(ApiSummariesTest, SummariesApiCapturesConfigInConstructor)
{
    SrsGoApiSummaries handler;
    EXPECT_TRUE(handler.config_ != NULL);
    EXPECT_TRUE(handler.config_ == (ISrsAppConfig *)_srs_config);
}

// The summaries API dumps the command line and work directory of its injected config.
VOID TEST(ApiSummariesTest, SummariesApiDumpsFromInjectedConfig)
{
    srs_error_t err;

    MockAppConfigForSummaries config;
    config.argv_ = "./objs/srs -c conf/api.conf";
    config.cwd_ = "/tmp/srs-api";

    SrsGoApiSummaries handler;
    handler.config_ = &config;

    MockResponseWriter writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> message(new MockHttpMessageForApiResponse());
    HELPER_EXPECT_SUCCESS(handler.serve_http(&writer, message.get()));

    string response = string(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
    size_t pos = response.find("\r\n\r\n");
    ASSERT_TRUE(pos != string::npos);

    SrsUniquePtr<SrsJsonAny> json(SrsJsonAny::loads(response.substr(pos + 4)));
    ASSERT_TRUE(json.get() && json->is_object());

    SrsJsonObject *self = find_summaries_self(json->to_object());
    ASSERT_TRUE(self != NULL);

    SrsJsonAny *prop = self->get_property("argv");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("./objs/srs -c conf/api.conf", prop->to_str().c_str());

    prop = self->get_property("cwd");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("/tmp/srs-api", prop->to_str().c_str());

    EXPECT_EQ(1, config.argv_count_);
    EXPECT_EQ(1, config.cwd_count_);

    handler.config_ = NULL;
}

VOID TEST(ReproduceIssue4609, GracefulDisconnectsDoNotIncrementErrors)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());

    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "livestream"));
    MockExpire conn;

    int graceful_codes[] = {
        ERROR_SOCKET_READ,
        ERROR_SOCKET_READ_FULLY,
        ERROR_SOCKET_WRITE,
        ERROR_SRT_IO,
        ERROR_HTTP_STREAM_EOF,
    };
    int nb_graceful_codes = sizeof(graceful_codes) / sizeof(int);

    for (int i = 0; i < nb_graceful_codes; i++) {
        stringstream ss;
        ss << "client-" << i;
        string client_id = ss.str();
        HELPER_EXPECT_SUCCESS(stat->on_client(client_id, req.get(), &conn, SrsRtmpConnPlay));

        srs_error_t graceful_close = srs_error_new(graceful_codes[i], "gracefully closed");
        EXPECT_TRUE(srs_is_client_gracefully_close(graceful_close) || srs_is_server_gracefully_close(graceful_close));
        stat->on_disconnect(client_id, graceful_close);
        srs_freep(graceful_close);
    }

    int64_t send_bytes = 0;
    int64_t recv_bytes = 0;
    int64_t nstreams = 0;
    int64_t nclients = 0;
    int64_t total_nclients = 0;
    int64_t nerrs = 0;
    HELPER_EXPECT_SUCCESS(stat->dumps_metrics(send_bytes, recv_bytes, nstreams, nclients, total_nclients, nerrs));

    EXPECT_EQ(0, nclients);
    EXPECT_EQ(nb_graceful_codes, total_nclients);
    EXPECT_EQ(0, nerrs);
}

// Mock ISrsHttpResponseReader implementation for SrsHttpHooks testing
MockHttpResponseReaderForHooks::MockHttpResponseReaderForHooks()
{
    content_ = "";
    read_pos_ = 0;
    eof_ = false;
}

MockHttpResponseReaderForHooks::~MockHttpResponseReaderForHooks()
{
}

srs_error_t MockHttpResponseReaderForHooks::read(void *buf, size_t size, ssize_t *nread)
{
    if (eof_ || read_pos_ >= content_.length()) {
        eof_ = true;
        return srs_error_new(-1, "EOF");
    }

    size_t remaining = content_.length() - read_pos_;
    size_t to_read = srs_min(size, remaining);
    memcpy(buf, content_.data() + read_pos_, to_read);
    read_pos_ += to_read;
    if (nread) {
        *nread = to_read;
    }

    if (read_pos_ >= content_.length()) {
        eof_ = true;
    }

    return srs_success;
}

bool MockHttpResponseReaderForHooks::eof()
{
    return eof_;
}

// Mock ISrsHttpMessage implementation for SrsHttpHooks testing
MockHttpMessageForHooks::MockHttpMessageForHooks()
{
    status_code_ = 200;
    body_content_ = "{\"code\":0}";
    body_reader_ = NULL;
}

MockHttpMessageForHooks::~MockHttpMessageForHooks()
{
    srs_freep(body_reader_);
}

uint8_t MockHttpMessageForHooks::method()
{
    return 0;
}

uint16_t MockHttpMessageForHooks::status_code()
{
    return status_code_;
}

std::string MockHttpMessageForHooks::method_str()
{
    return "POST";
}

std::string MockHttpMessageForHooks::url()
{
    return "";
}

std::string MockHttpMessageForHooks::host()
{
    return "";
}

std::string MockHttpMessageForHooks::path()
{
    return "";
}

std::string MockHttpMessageForHooks::query()
{
    return "";
}

std::string MockHttpMessageForHooks::ext()
{
    return "";
}

srs_error_t MockHttpMessageForHooks::body_read_all(std::string &body)
{
    body = body_content_;
    return srs_success;
}

ISrsHttpResponseReader *MockHttpMessageForHooks::body_reader()
{
    return body_reader_;
}

int64_t MockHttpMessageForHooks::content_length()
{
    return 0;
}

std::string MockHttpMessageForHooks::query_get(std::string key)
{
    return "";
}

int MockHttpMessageForHooks::request_header_count()
{
    return 0;
}

std::string MockHttpMessageForHooks::request_header_key_at(int index)
{
    return "";
}

std::string MockHttpMessageForHooks::request_header_value_at(int index)
{
    return "";
}

std::string MockHttpMessageForHooks::get_request_header(std::string name)
{
    return "";
}

ISrsRequest *MockHttpMessageForHooks::to_request(std::string vhost)
{
    return NULL;
}

bool MockHttpMessageForHooks::is_chunked()
{
    return false;
}

bool MockHttpMessageForHooks::is_keep_alive()
{
    return false;
}

bool MockHttpMessageForHooks::is_jsonp()
{
    return false;
}

std::string MockHttpMessageForHooks::jsonp()
{
    return "";
}

bool MockHttpMessageForHooks::require_crossdomain()
{
    return false;
}

srs_error_t MockHttpMessageForHooks::enter_infinite_chunked()
{
    return srs_success;
}

srs_error_t MockHttpMessageForHooks::end_infinite_chunked()
{
    return srs_success;
}

uint8_t MockHttpMessageForHooks::message_type()
{
    return 0;
}

bool MockHttpMessageForHooks::is_http_get()
{
    return false;
}

bool MockHttpMessageForHooks::is_http_put()
{
    return false;
}

bool MockHttpMessageForHooks::is_http_post()
{
    return true;
}

bool MockHttpMessageForHooks::is_http_delete()
{
    return false;
}

bool MockHttpMessageForHooks::is_http_options()
{
    return false;
}

std::string MockHttpMessageForHooks::uri()
{
    return "";
}

std::string MockHttpMessageForHooks::parse_rest_id(std::string pattern)
{
    return "";
}

SrsHttpHeader *MockHttpMessageForHooks::header()
{
    return NULL;
}

// Mock ISrsHttpClient implementation for SrsHttpHooks testing
MockHttpClientForHooks::MockHttpClientForHooks()
{
    initialize_called_ = false;
    post_called_ = false;
    get_called_ = false;
    port_ = 0;
    mock_response_ = NULL;
    initialize_error_ = srs_success;
    post_error_ = srs_success;
    get_error_ = srs_success;
}

MockHttpClientForHooks::~MockHttpClientForHooks()
{
}

srs_error_t MockHttpClientForHooks::initialize(std::string schema, std::string h, int p, srs_utime_t tm)
{
    initialize_called_ = true;
    schema_ = schema;
    host_ = h;
    port_ = p;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockHttpClientForHooks::get(std::string path, std::string req, ISrsHttpMessage **ppmsg)
{
    get_called_ = true;
    path_ = path;
    request_body_ = req;
    if (ppmsg && mock_response_) {
        *ppmsg = mock_response_;
    }
    return srs_error_copy(get_error_);
}

srs_error_t MockHttpClientForHooks::post(std::string path, std::string req, ISrsHttpMessage **ppmsg)
{
    post_called_ = true;
    path_ = path;
    request_body_ = req;
    if (ppmsg && mock_response_) {
        *ppmsg = mock_response_;
    }
    return srs_error_copy(post_error_);
}

void MockHttpClientForHooks::set_recv_timeout(srs_utime_t tm)
{
}

void MockHttpClientForHooks::kbps_sample(const char *label, srs_utime_t age)
{
}

// Mock ISrsAppFactory implementation for SrsHttpHooks testing
MockAppFactoryForHooks::MockAppFactoryForHooks()
{
    mock_http_client_ = NULL;
}

MockAppFactoryForHooks::~MockAppFactoryForHooks()
{
}

ISrsHttpClient *MockAppFactoryForHooks::create_http_client()
{
    return mock_http_client_;
}

// Mock ISrsStatistic implementation for SrsHttpHooks testing
MockStatisticForHooks::MockStatisticForHooks()
{
    server_id_ = "test_server_id";
    service_id_ = "test_service_id";
}

MockStatisticForHooks::~MockStatisticForHooks()
{
}

void MockStatisticForHooks::on_disconnect(std::string id, srs_error_t err)
{
}

srs_error_t MockStatisticForHooks::on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type)
{
    return srs_success;
}

srs_error_t MockStatisticForHooks::on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height)
{
    return srs_success;
}

srs_error_t MockStatisticForHooks::on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate,
                                                 SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    return srs_success;
}

void MockStatisticForHooks::on_stream_publish(ISrsRequest *req, std::string publisher_id)
{
}

void MockStatisticForHooks::on_stream_close(ISrsRequest *req)
{
}

void MockStatisticForHooks::kbps_add_delta(std::string id, ISrsKbpsDelta *delta)
{
}

void MockStatisticForHooks::kbps_sample()
{
}

srs_error_t MockStatisticForHooks::on_video_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

srs_error_t MockStatisticForHooks::on_audio_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

std::string MockStatisticForHooks::server_id()
{
    return server_id_;
}

std::string MockStatisticForHooks::service_id()
{
    return service_id_;
}

std::string MockStatisticForHooks::service_pid()
{
    return "test_pid";
}

SrsStatisticVhost *MockStatisticForHooks::find_vhost_by_id(std::string vid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForHooks::find_stream(std::string sid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForHooks::find_stream_by_url(std::string url)
{
    return NULL;
}

SrsStatisticClient *MockStatisticForHooks::find_client(std::string client_id)
{
    return NULL;
}

srs_error_t MockStatisticForHooks::dumps_vhosts(SrsJsonArray *arr)
{
    return srs_success;
}

srs_error_t MockStatisticForHooks::dumps_streams(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForHooks::dumps_clients(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForHooks::dumps_forwards(SrsJsonArray *arr, std::string stream_id, int start, int count)
{
    return srs_success;
}

int MockStatisticForHooks::count_forwards(std::string stream_id)
{
    return 0;
}

SrsStatisticForward *MockStatisticForHooks::find_forward(std::string forward_id)
{
    return NULL;
}

void MockStatisticForHooks::on_forward_start(std::string id, ISrsRequest *req, std::string url)
{
}

void MockStatisticForHooks::on_forward_state(std::string id, SrsForwardState state)
{
}

void MockStatisticForHooks::on_forward_stop(std::string id)
{
}

srs_error_t MockStatisticForHooks::dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    return srs_success;
}

// Mock factory that tracks HTTP client calls
class MockAppFactoryForHooksTest : public SrsAppFactory
{
public:
    bool initialize_called_;
    bool post_called_;
    std::string schema_;
    std::string host_;
    int port_;
    std::string path_;
    std::string request_body_;
    MockHttpClientForHooks *mock_http_client_;

public:
    MockAppFactoryForHooksTest()
    {
        initialize_called_ = false;
        post_called_ = false;
        port_ = 0;
        mock_http_client_ = NULL;
    }

    virtual ~MockAppFactoryForHooksTest()
    {
    }

    virtual ISrsHttpClient *create_http_client()
    {
        // If a specific mock client is set, return it
        if (mock_http_client_) {
            return mock_http_client_;
        }

        // Create a mock HTTP client that saves call information to the factory
        MockHttpClientForHooks *client = new MockHttpClientForHooks();

        // Create mock response
        MockHttpMessageForHooks *msg = new MockHttpMessageForHooks();
        msg->status_code_ = 200;
        msg->body_content_ = "{\"code\":0}";

        // Create mock body reader for GET requests (e.g., on_hls_notify)
        MockHttpResponseReaderForHooks *reader = new MockHttpResponseReaderForHooks();
        reader->content_ = "OK"; // Simple response body
        msg->body_reader_ = reader;

        client->mock_response_ = msg;

        return client;
    }
};

VOID TEST(HttpHooksTest, OnConnectSuccess)
{
    srs_error_t err;

    // Create mock factory that will track calls
    SrsUniquePtr<MockAppFactoryForHooksTest> mock_factory(new MockAppFactoryForHooksTest());

    // Create mock statistic
    SrsUniquePtr<MockStatisticForHooks> mock_stat(new MockStatisticForHooks());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    req->ip_ = "192.168.1.100";
    req->tcUrl_ = "rtmp://test.vhost/live";
    req->pageUrl_ = "http://example.com/player.html";
    req->param_ = "?token=abc123";

    // Create SrsHttpHooks and inject mocks
    SrsUniquePtr<SrsHttpHooks> hooks(new SrsHttpHooks());
    hooks->factory_ = mock_factory.get();
    hooks->stat_ = mock_stat.get();

    // Test on_connect with successful response
    std::string url = "http://127.0.0.1:8085/api/v1/clients";
    HELPER_EXPECT_SUCCESS(hooks->on_connect(url, req.get()));

    // Clean up - set injected fields to NULL to avoid double-free
    hooks->factory_ = NULL;
    hooks->stat_ = NULL;
}

VOID TEST(HttpHooksTest, OnCloseSuccess)
{
    // Create mock factory that will track calls
    SrsUniquePtr<MockAppFactoryForHooksTest> mock_factory(new MockAppFactoryForHooksTest());

    // Create mock statistic
    SrsUniquePtr<MockStatisticForHooks> mock_stat(new MockStatisticForHooks());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    req->ip_ = "192.168.1.100";
    req->vhost_ = "test.vhost";
    req->app_ = "live";

    // Create SrsHttpHooks and inject mocks
    SrsUniquePtr<SrsHttpHooks> hooks(new SrsHttpHooks());
    hooks->factory_ = mock_factory.get();
    hooks->stat_ = mock_stat.get();

    // Test on_close with successful response
    std::string url = "http://127.0.0.1:8085/api/v1/clients";
    int64_t send_bytes = 1024000;
    int64_t recv_bytes = 512000;
    hooks->on_close(url, req.get(), send_bytes, recv_bytes);

    // Clean up - set injected fields to NULL to avoid double-free
    hooks->factory_ = NULL;
    hooks->stat_ = NULL;
}

VOID TEST(HttpHooksTest, OnPublishSuccess)
{
    srs_error_t err;

    // Create mock factory that will track calls
    SrsUniquePtr<MockAppFactoryForHooksTest> mock_factory(new MockAppFactoryForHooksTest());

    // Create mock statistic
    SrsUniquePtr<MockStatisticForHooks> mock_stat(new MockStatisticForHooks());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    req->ip_ = "192.168.1.100";
    req->tcUrl_ = "rtmp://test.vhost/live";
    req->param_ = "?token=abc123";

    // Create SrsHttpHooks and inject mocks
    SrsUniquePtr<SrsHttpHooks> hooks(new SrsHttpHooks());
    hooks->factory_ = mock_factory.get();
    hooks->stat_ = mock_stat.get();

    // Test on_publish with successful response - major use scenario
    std::string url = "http://127.0.0.1:8085/api/v1/streams";
    HELPER_EXPECT_SUCCESS(hooks->on_publish(url, req.get()));

    // Clean up - set injected fields to NULL to avoid double-free
    hooks->factory_ = NULL;
    hooks->stat_ = NULL;
}

VOID TEST(HttpHooksTest, OnUnpublishSuccess)
{
    // Create mock factory that will track calls
    SrsUniquePtr<MockAppFactoryForHooksTest> mock_factory(new MockAppFactoryForHooksTest());

    // Create mock statistic
    SrsUniquePtr<MockStatisticForHooks> mock_stat(new MockStatisticForHooks());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    req->ip_ = "192.168.1.100";
    req->tcUrl_ = "rtmp://test.vhost/live";
    req->param_ = "?token=abc123";

    // Create SrsHttpHooks and inject mocks
    SrsUniquePtr<SrsHttpHooks> hooks(new SrsHttpHooks());
    hooks->factory_ = mock_factory.get();
    hooks->stat_ = mock_stat.get();

    // Test on_unpublish with successful response - major use scenario
    std::string url = "http://127.0.0.1:8085/api/v1/streams";
    hooks->on_unpublish(url, req.get());

    // Clean up - set injected fields to NULL to avoid double-free
    hooks->factory_ = NULL;
    hooks->stat_ = NULL;
}

VOID TEST(HttpHooksTest, OnPlaySuccess)
{
    srs_error_t err;

    // Create mock factory that will track calls
    SrsUniquePtr<MockAppFactoryForHooksTest> mock_factory(new MockAppFactoryForHooksTest());

    // Create mock statistic
    SrsUniquePtr<MockStatisticForHooks> mock_stat(new MockStatisticForHooks());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    req->ip_ = "192.168.1.100";
    req->tcUrl_ = "rtmp://test.vhost/live";
    req->param_ = "?token=abc123";
    req->pageUrl_ = "http://example.com/player.html";

    // Create SrsHttpHooks and inject mocks
    SrsUniquePtr<SrsHttpHooks> hooks(new SrsHttpHooks());
    hooks->factory_ = mock_factory.get();
    hooks->stat_ = mock_stat.get();

    // Test on_play with successful response - major use scenario
    std::string url = "http://127.0.0.1:8085/api/v1/sessions";
    HELPER_EXPECT_SUCCESS(hooks->on_play(url, req.get()));

    // Clean up - set injected fields to NULL to avoid double-free
    hooks->factory_ = NULL;
    hooks->stat_ = NULL;
}

VOID TEST(HttpHooksTest, OnStopSuccess)
{
    // Create mock factory that will track calls
    SrsUniquePtr<MockAppFactoryForHooksTest> mock_factory(new MockAppFactoryForHooksTest());

    // Create mock statistic
    SrsUniquePtr<MockStatisticForHooks> mock_stat(new MockStatisticForHooks());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    req->ip_ = "192.168.1.100";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->tcUrl_ = "rtmp://test.vhost/live";
    req->param_ = "?token=abc123";

    // Create SrsHttpHooks and inject mocks
    SrsUniquePtr<SrsHttpHooks> hooks(new SrsHttpHooks());
    hooks->factory_ = mock_factory.get();
    hooks->stat_ = mock_stat.get();

    // Test on_stop with successful response - major use scenario
    std::string url = "http://127.0.0.1:8085/api/v1/sessions";
    hooks->on_stop(url, req.get());

    // Clean up - set injected fields to NULL to avoid double-free
    hooks->factory_ = NULL;
    hooks->stat_ = NULL;
}

VOID TEST(HttpHooksTest, OnDvrSuccess)
{
    srs_error_t err;

    // Create mock factory that will track calls
    SrsUniquePtr<MockAppFactoryForHooksTest> mock_factory(new MockAppFactoryForHooksTest());

    // Create mock statistic
    SrsUniquePtr<MockStatisticForHooks> mock_stat(new MockStatisticForHooks());
    mock_stat->server_id_ = "server-123";
    mock_stat->service_id_ = "service-456";

    // Create mock config
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    req->ip_ = "192.168.1.100";
    req->tcUrl_ = "rtmp://test.vhost/live";
    req->param_ = "?token=abc123";

    // Create SrsHttpHooks and inject mocks
    SrsUniquePtr<SrsHttpHooks> hooks(new SrsHttpHooks());
    hooks->factory_ = mock_factory.get();
    hooks->stat_ = mock_stat.get();
    hooks->config_ = mock_config.get();

    // Test on_dvr with successful response - major use scenario
    // This covers DVR recording notification with file path
    SrsContextId cid;
    cid.set_value("client-789");
    std::string url = "http://127.0.0.1:8085/api/v1/dvrs";
    std::string file = "/data/dvr/test.vhost/live/stream1/2025-01-15/recording.flv";
    HELPER_EXPECT_SUCCESS(hooks->on_dvr(cid, url, req.get(), file));

    // Clean up - set injected fields to NULL to avoid double-free
    hooks->factory_ = NULL;
    hooks->stat_ = NULL;
    hooks->config_ = NULL;
}

VOID TEST(HttpHooksTest, OnHlsSuccess)
{
    srs_error_t err;

    // Create mock factory that will track calls
    SrsUniquePtr<MockAppFactoryForHooksTest> mock_factory(new MockAppFactoryForHooksTest());

    // Create mock statistic
    SrsUniquePtr<MockStatisticForHooks> mock_stat(new MockStatisticForHooks());
    mock_stat->server_id_ = "server-123";
    mock_stat->service_id_ = "service-456";

    // Create mock config
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    req->ip_ = "192.168.1.100";
    req->tcUrl_ = "rtmp://test.vhost/live";
    req->param_ = "?token=abc123";

    // Create SrsHttpHooks and inject mocks
    SrsUniquePtr<SrsHttpHooks> hooks(new SrsHttpHooks());
    hooks->factory_ = mock_factory.get();
    hooks->stat_ = mock_stat.get();
    hooks->config_ = mock_config.get();

    // Test on_hls with successful response - major use scenario
    // This covers HLS segment notification with all typical parameters
    SrsContextId cid;
    cid.set_value("client-789");
    std::string url = "http://127.0.0.1:8085/api/v1/hls";
    std::string file = "/data/hls/test.vhost/live/stream1/segment-123.ts";
    std::string ts_url = "segment-123.ts";
    std::string m3u8 = "/data/hls/test.vhost/live/stream1/playlist.m3u8";
    std::string m3u8_url = "http://127.0.0.1:8080/live/stream1/playlist.m3u8";
    int sn = 123;
    srs_utime_t duration = 10 * SRS_UTIME_SECONDS; // 10 seconds

    HELPER_EXPECT_SUCCESS(hooks->on_hls(cid, url, req.get(), file, ts_url, m3u8, m3u8_url, sn, duration));

    // Clean up - set injected fields to NULL to avoid double-free
    hooks->factory_ = NULL;
    hooks->stat_ = NULL;
    hooks->config_ = NULL;
}

VOID TEST(HttpHooksTest, OnHlsNotifySuccess)
{
    srs_error_t err;

    // Create mock factory that will track calls
    SrsUniquePtr<MockAppFactoryForHooksTest> mock_factory(new MockAppFactoryForHooksTest());

    // Create mock statistic
    SrsUniquePtr<MockStatisticForHooks> mock_stat(new MockStatisticForHooks());
    mock_stat->server_id_ = "server-123";
    mock_stat->service_id_ = "service-456";

    // Create mock config
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    req->app_ = "live";
    req->stream_ = "stream1";
    req->param_ = "?token=abc123";

    // Create SrsHttpHooks and inject mocks
    SrsUniquePtr<SrsHttpHooks> hooks(new SrsHttpHooks());
    hooks->factory_ = mock_factory.get();
    hooks->stat_ = mock_stat.get();
    hooks->config_ = mock_config.get();

    // Test on_hls_notify with successful response - major use scenario
    // This covers HLS segment notification with URL template variable replacement
    SrsContextId cid;
    cid.set_value("client-789");
    std::string url = "http://127.0.0.1:8085/api/v1/hls/notify?server=[server_id]&service=[service_id]&app=[app]&stream=[stream]&ts=[ts_url]&param=[param]";
    std::string ts_url = "segment-123.ts";
    int nb_notify = 1024; // Read up to 1KB from response

    HELPER_EXPECT_SUCCESS(hooks->on_hls_notify(cid, url, req.get(), ts_url, nb_notify));

    // Clean up - set injected fields to NULL to avoid double-free
    hooks->factory_ = NULL;
    hooks->stat_ = NULL;
    hooks->config_ = NULL;
}

VOID TEST(HttpHooksTest, DiscoverCoWorkersSuccess)
{
    srs_error_t err;

    // Create mock factory that returns HTTP client with cluster discovery response
    SrsUniquePtr<MockAppFactoryForHooksTest> mock_factory(new MockAppFactoryForHooksTest());

    // Create SrsHttpHooks and inject mock factory
    SrsUniquePtr<SrsHttpHooks> hooks(new SrsHttpHooks());
    hooks->factory_ = mock_factory.get();

    // Override the mock HTTP client response to return cluster discovery JSON
    // The response should contain: {"data": {"origin": {"ip": "192.168.1.10", "port": 1935}}}
    MockHttpClientForHooks *mock_client = new MockHttpClientForHooks();
    MockHttpMessageForHooks *msg = new MockHttpMessageForHooks();
    msg->status_code_ = 200;
    msg->body_content_ = "{\"code\":0,\"data\":{\"origin\":{\"ip\":\"192.168.1.10\",\"port\":1935}}}";

    MockHttpResponseReaderForHooks *reader = new MockHttpResponseReaderForHooks();
    reader->content_ = msg->body_content_;
    msg->body_reader_ = reader;

    mock_client->mock_response_ = msg;
    mock_factory->mock_http_client_ = mock_client;

    // Test discover_co_workers with successful response - major use scenario
    // This covers origin cluster discovery with host and port extraction
    std::string url = "http://127.0.0.1:8085/api/v1/clusters";
    std::string host;
    int port = 0;

    HELPER_EXPECT_SUCCESS(hooks->discover_co_workers(url, host, port));

    // Verify the parsed host and port
    EXPECT_STREQ("192.168.1.10", host.c_str());
    EXPECT_EQ(1935, port);

    // Clean up - set injected fields to NULL to avoid double-free
    // Note: mock_client is already freed by SrsUniquePtr in discover_co_workers
    hooks->factory_ = NULL;
    mock_factory->mock_http_client_ = NULL;
}

VOID TEST(HttpHooksTest, OnForwardBackendSuccess)
{
    srs_error_t err;

    // Create mock factory that returns HTTP client with forward backend response
    SrsUniquePtr<MockAppFactoryForHooksTest> mock_factory(new MockAppFactoryForHooksTest());

    // Create mock statistic
    SrsUniquePtr<MockStatisticForHooks> mock_stat(new MockStatisticForHooks());

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    req->ip_ = "192.168.1.100";
    req->tcUrl_ = "rtmp://test.vhost/live";
    req->param_ = "?token=abc123";

    // Create SrsHttpHooks and inject mocks
    SrsUniquePtr<SrsHttpHooks> hooks(new SrsHttpHooks());
    hooks->factory_ = mock_factory.get();
    hooks->stat_ = mock_stat.get();

    // Override the mock HTTP client response to return forward backend JSON with RTMP URLs
    // The response should contain: {"data": {"urls": ["rtmp://origin1/live/stream1", "rtmp://origin2/live/stream1"]}}
    MockHttpClientForHooks *mock_client = new MockHttpClientForHooks();
    MockHttpMessageForHooks *msg = new MockHttpMessageForHooks();
    msg->status_code_ = 200;
    msg->body_content_ = "{\"code\":0,\"data\":{\"urls\":[\"rtmp://192.168.1.10:1935/live/stream1\",\"rtmp://192.168.1.11:1935/live/stream1\"]}}";

    MockHttpResponseReaderForHooks *reader = new MockHttpResponseReaderForHooks();
    reader->content_ = msg->body_content_;
    msg->body_reader_ = reader;

    mock_client->mock_response_ = msg;
    mock_factory->mock_http_client_ = mock_client;

    // Test on_forward_backend with successful response - major use scenario
    // This covers forward backend discovery with RTMP URL extraction
    std::string url = "http://127.0.0.1:8085/api/v1/forward";
    std::vector<std::string> rtmp_urls;

    HELPER_EXPECT_SUCCESS(hooks->on_forward_backend(url, req.get(), rtmp_urls));

    // Verify the parsed RTMP URLs
    EXPECT_EQ(2, (int)rtmp_urls.size());
    EXPECT_STREQ("rtmp://192.168.1.10:1935/live/stream1", rtmp_urls[0].c_str());
    EXPECT_STREQ("rtmp://192.168.1.11:1935/live/stream1", rtmp_urls[1].c_str());

    // Clean up - set injected fields to NULL to avoid double-free
    // Note: mock_client is already freed by SrsUniquePtr in on_forward_backend
    hooks->factory_ = NULL;
    hooks->stat_ = NULL;
    mock_factory->mock_http_client_ = NULL;
}

// The ids of the forwards in a dump, in order.
static vector<string> utest_forward_dump_ids(SrsJsonArray *arr)
{
    vector<string> ids;
    for (int i = 0; i < arr->count(); i++) {
        SrsJsonAny *any = arr->at(i);
        SrsJsonAny *id = any->is_object() ? any->to_object()->get_property("id") : NULL;
        ids.push_back(id && id->is_string() ? id->to_str() : string("?"));
    }
    return ids;
}

// The ids of the forwards the statistic dumps for the stream and page, joined by spaces.
static string utest_forward_dump(SrsStatistic *stat, string stream_id, int start, int count)
{
    SrsUniquePtr<SrsJsonArray> arr(SrsJsonAny::array());
    srs_error_t err = stat->dumps_forwards(arr.get(), stream_id, start, count);
    if (err != srs_success) {
        string desc = "error " + srs_error_desc(err);
        srs_freep(err);
        return desc;
    }

    vector<string> ids = utest_forward_dump_ids(arr.get());
    string joined;
    for (int i = 0; i < (int)ids.size(); i++) {
        joined += (i ? " " : "") + ids[i];
    }
    return joined;
}

// A forward starts connecting, with the id, publishing stream and url the forwarder reports; each
// state change stamps the time it entered the state; stop removes it. Unknown ids are ignored.
VOID TEST(StatisticForwardTest, StartStateAndStop)
{
    MockClockForPithyPrint clock;
    clock.now_ = 1000 * SRS_UTIME_SECONDS;

    SrsStatistic stat;
    stat.clk_ = &clock;

    MockSrsRequest req("test.vhost", "live", "livestream");
    stat.on_forward_start("fwd-1", &req, "rtmp://127.0.0.1:19350/live/livestream?token=abc&vhost=test.vhost");

    SrsStatisticStream *stream = stat.find_stream_by_url(req.get_stream_url());
    ASSERT_TRUE(stream != NULL);

    SrsStatisticForward *forward = stat.find_forward("fwd-1");
    ASSERT_TRUE(forward != NULL);
    EXPECT_STREQ("fwd-1", forward->id_.c_str());
    EXPECT_STREQ(stream->id_.c_str(), forward->stream_id_.c_str());
    EXPECT_STREQ(stream->vhost_->id_.c_str(), forward->vhost_id_.c_str());
    EXPECT_STREQ("live", forward->app_.c_str());
    EXPECT_STREQ("livestream", forward->name_.c_str());
    EXPECT_STREQ("rtmp://127.0.0.1:19350/live/livestream?token=abc&vhost=test.vhost", forward->url_.c_str());
    EXPECT_EQ(SrsForwardStateConnecting, forward->state_);
    EXPECT_EQ(1000 * SRS_UTIME_SECONDS, forward->state_at_);

    clock.now_ = 1001 * SRS_UTIME_SECONDS;
    stat.on_forward_state("fwd-1", SrsForwardStateForwarding);
    EXPECT_EQ(SrsForwardStateForwarding, forward->state_);
    EXPECT_EQ(1001 * SRS_UTIME_SECONDS, forward->state_at_);

    clock.now_ = 1004 * SRS_UTIME_SECONDS;
    stat.on_forward_state("fwd-1", SrsForwardStateRetrying);
    EXPECT_EQ(SrsForwardStateRetrying, forward->state_);
    EXPECT_EQ(1004 * SRS_UTIME_SECONDS, forward->state_at_);

    clock.now_ = 1007 * SRS_UTIME_SECONDS;
    stat.on_forward_state("fwd-1", SrsForwardStateConnecting);
    EXPECT_EQ(SrsForwardStateConnecting, forward->state_);
    EXPECT_EQ(1007 * SRS_UTIME_SECONDS, forward->state_at_);

    // An unknown id changes nothing.
    clock.now_ = 1010 * SRS_UTIME_SECONDS;
    stat.on_forward_state("fwd-x", SrsForwardStateForwarding);
    stat.on_forward_stop("fwd-x");
    EXPECT_TRUE(stat.find_forward("fwd-x") == NULL);
    ASSERT_TRUE(stat.find_forward("fwd-1") == forward);
    EXPECT_EQ(SrsForwardStateConnecting, forward->state_);
    EXPECT_EQ(1007 * SRS_UTIME_SECONDS, forward->state_at_);

    stat.on_forward_stop("fwd-1");
    EXPECT_TRUE(stat.find_forward("fwd-1") == NULL);

    stat.clk_ = NULL;
}

// The names of the states in the HTTP API.
VOID TEST(StatisticForwardTest, StateNames)
{
    EXPECT_STREQ("connecting", srs_forward_state_string(SrsForwardStateConnecting).c_str());
    EXPECT_STREQ("forwarding", srs_forward_state_string(SrsForwardStateForwarding).c_str());
    EXPECT_STREQ("retrying", srs_forward_state_string(SrsForwardStateRetrying).c_str());
}

// A forward dumps exactly its id, its stream (the stream id, vhost id, app and name), the full
// destination url, the state and the Unix time in ms it entered the state.
VOID TEST(StatisticForwardTest, DumpsExactFields)
{
    srs_error_t err;

    MockClockForPithyPrint clock;
    clock.now_ = 1760000000000LL * SRS_UTIME_MILLISECONDS;

    SrsStatistic stat;
    stat.clk_ = &clock;

    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    stat.on_forward_start("fwd-1", &req, "rtmp://127.0.0.1:19350/live/livestream?token=abc");
    clock.now_ = 1760000000123LL * SRS_UTIME_MILLISECONDS;
    stat.on_forward_state("fwd-1", SrsForwardStateForwarding);

    SrsStatisticStream *stream = stat.find_stream_by_url(req.get_stream_url());
    ASSERT_TRUE(stream != NULL);

    SrsUniquePtr<SrsJsonArray> arr(SrsJsonAny::array());
    HELPER_EXPECT_SUCCESS(stat.dumps_forwards(arr.get(), "", 0, 10));
    ASSERT_EQ(1, arr->count());
    ASSERT_TRUE(arr->at(0)->is_object());
    SrsJsonObject *obj = arr->at(0)->to_object();

    const char *keys[] = {"id", "stream", "vhost", "app", "name", "url", "state", "state_at"};
    ASSERT_EQ(8, obj->count());
    for (int i = 0; i < 8; i++) {
        EXPECT_STREQ(keys[i], obj->key_at(i).c_str()) << "#" << i;
    }

    string values[] = {"fwd-1", stream->id_, stream->vhost_->id_, "live", "livestream",
                       "rtmp://127.0.0.1:19350/live/livestream?token=abc", "forwarding"};
    for (int i = 0; i < 7; i++) {
        SrsJsonAny *prop = obj->get_property(keys[i]);
        ASSERT_TRUE(prop && prop->is_string()) << keys[i];
        EXPECT_STREQ(values[i].c_str(), prop->to_str().c_str()) << keys[i];
    }

    SrsJsonAny *state_at = obj->get_property("state_at");
    ASSERT_TRUE(state_at && state_at->is_integer());
    EXPECT_EQ(1760000000123LL, state_at->to_integer());

    stat.clk_ = NULL;
}

// The stream filter dumps only the forwards of that stream; an unknown stream dumps none, and no
// filter dumps all.
VOID TEST(StatisticForwardTest, DumpsFilterByStream)
{
    SrsStatistic stat;

    MockSrsRequest a("__defaultVhost__", "live", "a");
    MockSrsRequest b("__defaultVhost__", "live", "b");
    stat.on_forward_start("fwd-a1", &a, "rtmp://127.0.0.1:19350/live/a");
    stat.on_forward_start("fwd-a2", &a, "rtmp://127.0.0.1:19351/live/a");
    stat.on_forward_start("fwd-b1", &b, "rtmp://127.0.0.1:19350/live/b");

    SrsStatisticStream *sa = stat.find_stream_by_url(a.get_stream_url());
    SrsStatisticStream *sb = stat.find_stream_by_url(b.get_stream_url());
    ASSERT_TRUE(sa != NULL);
    ASSERT_TRUE(sb != NULL);

    EXPECT_STREQ("fwd-a1 fwd-a2", utest_forward_dump(&stat, sa->id_, 0, 10).c_str());
    EXPECT_STREQ("fwd-b1", utest_forward_dump(&stat, sb->id_, 0, 10).c_str());
    EXPECT_STREQ("", utest_forward_dump(&stat, "vid-unknown", 0, 10).c_str());
    EXPECT_STREQ("fwd-a1 fwd-a2 fwd-b1", utest_forward_dump(&stat, "", 0, 10).c_str());
}

// Start skips and count limits the forwards, after the stream filter.
VOID TEST(StatisticForwardTest, DumpsPagination)
{
    SrsStatistic stat;

    MockSrsRequest a("__defaultVhost__", "live", "a");
    MockSrsRequest b("__defaultVhost__", "live", "b");
    stat.on_forward_start("fwd-1", &a, "rtmp://127.0.0.1:19351/live/a");
    stat.on_forward_start("fwd-2", &a, "rtmp://127.0.0.1:19352/live/a");
    stat.on_forward_start("fwd-3", &b, "rtmp://127.0.0.1:19353/live/b");
    stat.on_forward_start("fwd-4", &a, "rtmp://127.0.0.1:19354/live/a");
    stat.on_forward_start("fwd-5", &a, "rtmp://127.0.0.1:19355/live/a");

    SrsStatisticStream *sa = stat.find_stream_by_url(a.get_stream_url());
    ASSERT_TRUE(sa != NULL);

    EXPECT_STREQ("fwd-1 fwd-2", utest_forward_dump(&stat, "", 0, 2).c_str());
    EXPECT_STREQ("fwd-3 fwd-4", utest_forward_dump(&stat, "", 2, 2).c_str());
    EXPECT_STREQ("fwd-5", utest_forward_dump(&stat, "", 4, 10).c_str());
    EXPECT_STREQ("", utest_forward_dump(&stat, "", 5, 10).c_str());

    // The forward of the other stream is neither dumped nor counted.
    EXPECT_STREQ("fwd-2 fwd-4", utest_forward_dump(&stat, sa->id_, 1, 2).c_str());
    EXPECT_STREQ("fwd-5", utest_forward_dump(&stat, sa->id_, 3, 10).c_str());
}

// Closing a stream, on unpublish, removes every forward of it and keeps the other streams' ones.
VOID TEST(StatisticForwardTest, StreamCloseRemovesItsForwards)
{
    SrsStatistic stat;

    MockSrsRequest a("__defaultVhost__", "live", "a");
    MockSrsRequest b("__defaultVhost__", "live", "b");
    stat.on_stream_publish(&a, "cid-a");
    stat.on_stream_publish(&b, "cid-b");
    stat.on_forward_start("fwd-a1", &a, "rtmp://127.0.0.1:19350/live/a");
    stat.on_forward_start("fwd-a2", &a, "rtmp://127.0.0.1:19351/live/a");
    stat.on_forward_start("fwd-b1", &b, "rtmp://127.0.0.1:19350/live/b");

    stat.on_stream_close(&a);

    EXPECT_TRUE(stat.find_forward("fwd-a1") == NULL);
    EXPECT_TRUE(stat.find_forward("fwd-a2") == NULL);
    EXPECT_TRUE(stat.find_forward("fwd-b1") != NULL);
    EXPECT_STREQ("fwd-b1", utest_forward_dump(&stat, "", 0, 10).c_str());
}

// The JSON body of an HTTP API response, or NULL if it is not JSON.
static SrsJsonAny *utest_forward_api_body(MockResponseWriter *w)
{
    string response = string(w->io.out_buffer.bytes(), w->io.out_buffer.length());
    size_t pos = response.find("\r\n\r\n");
    if (pos == string::npos) {
        return NULL;
    }
    return SrsJsonAny::loads(response.substr(pos + 4));
}

// Serve one request of the method and url by the forwards API, on the statistic.
static srs_error_t utest_forward_api_serve(SrsStatistic *stat, llhttp_method_t method, string url, MockResponseWriter *w)
{
    srs_error_t err = srs_success;

    SrsGoApiForwards api;
    api.stat_ = stat;
    api.entry_ = new SrsHttpMuxEntry();
    api.entry_->pattern = "/api/v1/forwards/";

    SrsUniquePtr<SrsHttpMessage> req(new SrsHttpMessage());
    if ((err = req->set_url(url, false)) == srs_success) {
        req->set_basic(HTTP_REQUEST, method, (llhttp_status_t)200, -1);
        err = api.serve_http(w, req.get());
    }

    api.stat_ = NULL;
    srs_freep(api.entry_);
    return err;
}

// The ids of the forwards array of a list response, joined by spaces, or "?" if there is none.
static string utest_forward_api_ids(SrsJsonObject *obj)
{
    SrsJsonAny *forwards = obj->get_property("forwards");
    if (!forwards || !forwards->is_array()) {
        return "?";
    }

    vector<string> ids = utest_forward_dump_ids(forwards->to_array());
    string joined;
    for (int i = 0; i < (int)ids.size(); i++) {
        joined += (i ? " " : "") + ids[i];
    }
    return joined;
}

// The total of a list response, or -1 if there is none.
static int utest_forward_api_total(SrsJsonObject *obj)
{
    SrsJsonAny *total = obj->get_property("total");
    if (!total || !total->is_integer()) {
        return -1;
    }
    return (int)total->to_integer();
}

// GET /api/v1/forwards/ lists every forward, with the code, server, service and pid like the
// other APIs.
VOID TEST(ApiForwardsTest, ListsEveryForward)
{
    srs_error_t err;

    SrsStatistic stat;
    MockSrsRequest a("__defaultVhost__", "live", "a");
    MockSrsRequest b("__defaultVhost__", "live", "b");
    stat.on_forward_start("fwd-a1", &a, "rtmp://127.0.0.1:19350/live/a");
    stat.on_forward_start("fwd-a2", &a, "rtmp://127.0.0.1:19351/live/a?token=abc");
    stat.on_forward_start("fwd-b1", &b, "rtmp://127.0.0.1:19350/live/b");

    MockResponseWriter w;
    HELPER_EXPECT_SUCCESS(utest_forward_api_serve(&stat, HTTP_GET, "http://127.0.0.1/api/v1/forwards/", &w));

    SrsUniquePtr<SrsJsonAny> json(utest_forward_api_body(&w));
    ASSERT_TRUE(json.get() && json->is_object());
    SrsJsonObject *obj = json->to_object();

    SrsJsonAny *prop = obj->get_property("code");
    ASSERT_TRUE(prop && prop->is_integer());
    EXPECT_EQ(ERROR_SUCCESS, prop->to_integer());
    prop = obj->get_property("server");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ(stat.server_id().c_str(), prop->to_str().c_str());
    prop = obj->get_property("service");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ(stat.service_id().c_str(), prop->to_str().c_str());
    prop = obj->get_property("pid");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ(stat.service_pid().c_str(), prop->to_str().c_str());

    EXPECT_STREQ("fwd-a1 fwd-a2 fwd-b1", utest_forward_api_ids(obj).c_str());
    EXPECT_EQ(3, utest_forward_api_total(obj));

    // Each one is the forward's dump.
    SrsJsonArray *forwards = obj->get_property("forwards")->to_array();
    ASSERT_TRUE(forwards->at(1)->is_object());
    prop = forwards->at(1)->to_object()->get_property("url");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("rtmp://127.0.0.1:19351/live/a?token=abc", prop->to_str().c_str());
    prop = forwards->at(1)->to_object()->get_property("state");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("connecting", prop->to_str().c_str());
}

// ?stream= lists only the forwards of that stream, with start and count after the filter, and
// count defaults to 10. The total is the number of forwards after the filter, whatever the start
// and count, so the client knows how many pages there are.
VOID TEST(ApiForwardsTest, FiltersByStreamAndPaginates)
{
    srs_error_t err;

    SrsStatistic stat;
    MockSrsRequest a("__defaultVhost__", "live", "a");
    MockSrsRequest b("__defaultVhost__", "live", "b");
    stat.on_forward_start("fwd-a1", &a, "rtmp://127.0.0.1:19350/live/a");
    stat.on_forward_start("fwd-a2", &a, "rtmp://127.0.0.1:19351/live/a");
    stat.on_forward_start("fwd-b1", &b, "rtmp://127.0.0.1:19350/live/b");
    for (int i = 0; i < 11; i++) {
        stat.on_forward_start("fwd-c" + srs_strconv_format_int(10 + i), &b, "rtmp://127.0.0.1:19352/live/b");
    }

    SrsStatisticStream *sa = stat.find_stream_by_url(a.get_stream_url());
    ASSERT_TRUE(sa != NULL);

    if (true) {
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(utest_forward_api_serve(&stat, HTTP_GET, "http://127.0.0.1/api/v1/forwards/?stream=" + sa->id_, &w));
        SrsUniquePtr<SrsJsonAny> json(utest_forward_api_body(&w));
        ASSERT_TRUE(json.get() && json->is_object());
        EXPECT_STREQ("fwd-a1 fwd-a2", utest_forward_api_ids(json->to_object()).c_str());
        EXPECT_EQ(2, utest_forward_api_total(json->to_object()));
    }

    if (true) {
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(utest_forward_api_serve(&stat, HTTP_GET, "http://127.0.0.1/api/v1/forwards/?stream=" + sa->id_ + "&start=1&count=1", &w));
        SrsUniquePtr<SrsJsonAny> json(utest_forward_api_body(&w));
        ASSERT_TRUE(json.get() && json->is_object());
        EXPECT_STREQ("fwd-a2", utest_forward_api_ids(json->to_object()).c_str());
        EXPECT_EQ(2, utest_forward_api_total(json->to_object()));
    }

    // A start past the end lists nothing, and still has the total.
    if (true) {
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(utest_forward_api_serve(&stat, HTTP_GET, "http://127.0.0.1/api/v1/forwards/?stream=" + sa->id_ + "&start=5", &w));
        SrsUniquePtr<SrsJsonAny> json(utest_forward_api_body(&w));
        ASSERT_TRUE(json.get() && json->is_object());
        EXPECT_STREQ("", utest_forward_api_ids(json->to_object()).c_str());
        EXPECT_EQ(2, utest_forward_api_total(json->to_object()));
    }

    if (true) {
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(utest_forward_api_serve(&stat, HTTP_GET, "http://127.0.0.1/api/v1/forwards/?stream=vid-unknown", &w));
        SrsUniquePtr<SrsJsonAny> json(utest_forward_api_body(&w));
        ASSERT_TRUE(json.get() && json->is_object());
        EXPECT_STREQ("", utest_forward_api_ids(json->to_object()).c_str());
        EXPECT_EQ(0, utest_forward_api_total(json->to_object()));
    }

    // 14 forwards in all, 10 by default.
    if (true) {
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(utest_forward_api_serve(&stat, HTTP_GET, "http://127.0.0.1/api/v1/forwards/", &w));
        SrsUniquePtr<SrsJsonAny> json(utest_forward_api_body(&w));
        ASSERT_TRUE(json.get() && json->is_object());
        EXPECT_STREQ("fwd-a1 fwd-a2 fwd-b1 fwd-c10 fwd-c11 fwd-c12 fwd-c13 fwd-c14 fwd-c15 fwd-c16",
                     utest_forward_api_ids(json->to_object()).c_str());
        EXPECT_EQ(14, utest_forward_api_total(json->to_object()));
    }

    // The last page of the 14 forwards.
    if (true) {
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(utest_forward_api_serve(&stat, HTTP_GET, "http://127.0.0.1/api/v1/forwards/?start=10", &w));
        SrsUniquePtr<SrsJsonAny> json(utest_forward_api_body(&w));
        ASSERT_TRUE(json.get() && json->is_object());
        EXPECT_STREQ("fwd-c17 fwd-c18 fwd-c19 fwd-c20", utest_forward_api_ids(json->to_object()).c_str());
        EXPECT_EQ(14, utest_forward_api_total(json->to_object()));
    }
}

// GET /api/v1/forwards/{id} returns that forward as forward.
VOID TEST(ApiForwardsTest, GetsOneById)
{
    srs_error_t err;

    SrsStatistic stat;
    MockSrsRequest a("__defaultVhost__", "live", "a");
    stat.on_forward_start("fwd-a1", &a, "rtmp://127.0.0.1:19350/live/a");
    stat.on_forward_start("fwd-a2", &a, "rtmp://127.0.0.1:19351/live/a?token=abc");

    MockResponseWriter w;
    HELPER_EXPECT_SUCCESS(utest_forward_api_serve(&stat, HTTP_GET, "http://127.0.0.1/api/v1/forwards/fwd-a2", &w));

    SrsUniquePtr<SrsJsonAny> json(utest_forward_api_body(&w));
    ASSERT_TRUE(json.get() && json->is_object());
    SrsJsonObject *obj = json->to_object();

    SrsJsonAny *prop = obj->get_property("code");
    ASSERT_TRUE(prop && prop->is_integer());
    EXPECT_EQ(ERROR_SUCCESS, prop->to_integer());
    EXPECT_TRUE(obj->get_property("forwards") == NULL);

    SrsJsonAny *forward = obj->get_property("forward");
    ASSERT_TRUE(forward && forward->is_object());
    prop = forward->to_object()->get_property("id");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("fwd-a2", prop->to_str().c_str());
    prop = forward->to_object()->get_property("url");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("rtmp://127.0.0.1:19351/live/a?token=abc", prop->to_str().c_str());
}

// An unknown id returns the forward-not-found code, like an unknown client.
VOID TEST(ApiForwardsTest, UnknownIdReturnsErrorCode)
{
    srs_error_t err;

    SrsStatistic stat;
    MockSrsRequest a("__defaultVhost__", "live", "a");
    stat.on_forward_start("fwd-a1", &a, "rtmp://127.0.0.1:19350/live/a");

    MockResponseWriter w;
    HELPER_EXPECT_SUCCESS(utest_forward_api_serve(&stat, HTTP_GET, "http://127.0.0.1/api/v1/forwards/fwd-x", &w));

    SrsUniquePtr<SrsJsonAny> json(utest_forward_api_body(&w));
    ASSERT_TRUE(json.get() && json->is_object());
    SrsJsonAny *prop = json->to_object()->get_property("code");
    ASSERT_TRUE(prop && prop->is_integer());
    EXPECT_EQ(ERROR_RTMP_FORWARD_NOT_FOUND, prop->to_integer());
}

// The API is read-only: POST, PUT and DELETE return 405, and a DELETE leaves the forward running.
VOID TEST(ApiForwardsTest, OtherMethodsNotAllowed)
{
    srs_error_t err;

    SrsStatistic stat;
    MockSrsRequest a("__defaultVhost__", "live", "a");
    stat.on_forward_start("fwd-a1", &a, "rtmp://127.0.0.1:19350/live/a");

    llhttp_method_t methods[] = {HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_DELETE};
    const char *urls[] = {"http://127.0.0.1/api/v1/forwards/", "http://127.0.0.1/api/v1/forwards/fwd-a1",
                          "http://127.0.0.1/api/v1/forwards/fwd-a1", "http://127.0.0.1/api/v1/forwards/"};
    for (int i = 0; i < 4; i++) {
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(utest_forward_api_serve(&stat, methods[i], urls[i], &w));

        string response = string(w.io.out_buffer.bytes(), w.io.out_buffer.length());
        EXPECT_TRUE(response.find("HTTP/1.1 405") == 0) << "#" << i << " " << response;
    }

    EXPECT_TRUE(stat.find_forward("fwd-a1") != NULL);
}

// /api/v1/ lists the forwards API.
VOID TEST(ApiForwardsTest, V1ListsForwards)
{
    srs_error_t err;

    SrsStatistic stat;
    SrsGoApiV1 v1;
    v1.stat_ = &stat;

    MockResponseWriter w;
    SrsUniquePtr<SrsHttpMessage> req(new SrsHttpMessage());
    HELPER_EXPECT_SUCCESS(req->set_url("http://127.0.0.1/api/v1/", false));
    HELPER_EXPECT_SUCCESS(v1.serve_http(&w, req.get()));

    SrsUniquePtr<SrsJsonAny> json(utest_forward_api_body(&w));
    ASSERT_TRUE(json.get() && json->is_object());
    SrsJsonAny *urls = json->to_object()->get_property("urls");
    ASSERT_TRUE(urls && urls->is_object());
    SrsJsonAny *forwards = urls->to_object()->get_property("forwards");
    ASSERT_TRUE(forwards && forwards->is_string());
    EXPECT_FALSE(forwards->to_str().empty());

    v1.stat_ = NULL;
}
