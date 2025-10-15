//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_DASH_HPP
#define SRS_APP_DASH_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_app_fragment.hpp>

class ISrsRequest;
class SrsOriginHub;
class ISrsOriginHub;
class SrsMediaPacket;
class SrsFormat;
class SrsFileWriter;
class ISrsFileWriter;
class SrsMpdWriter;
class ISrsMpdWriter;
class SrsMp4M2tsInitEncoder;
class ISrsMp4M2tsInitEncoder;
class SrsMp4M2tsSegmentEncoder;
class ISrsMp4M2tsSegmentEncoder;
class SrsFragment;
class ISrsFragment;
class ISrsAppFactory;
class ISrsDashController;
class ISrsFragmentWindow;
class ISrsAppConfig;

// The init mp4 fragment interface.
class ISrsInitMp4 : public ISrsFragment
{
public:
    ISrsInitMp4();
    virtual ~ISrsInitMp4();

public:
    // Write the init mp4 file, with the tid(track id).
    virtual srs_error_t write(SrsFormat *format, bool video, int tid) = 0;
};

// The init mp4 for FMP4.
class SrsInitMp4 : public ISrsInitMp4
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFileWriter *fw_;
    ISrsMp4M2tsInitEncoder *init_;
    ISrsFragment *fragment_;

public:
    SrsInitMp4();
    virtual ~SrsInitMp4();

public:
    // Write the init mp4 file, with the tid(track id).
    virtual srs_error_t write(SrsFormat *format, bool video, int tid);

public:
    // ISrsFragment interface implementations - delegate to fragment_
    virtual void set_path(std::string v);
    virtual std::string tmppath();
    virtual srs_error_t rename();
    virtual void append(int64_t dts);
    virtual srs_error_t create_dir();
    virtual void set_number(uint64_t n);
    virtual uint64_t number();
    virtual srs_utime_t duration();
    virtual srs_error_t unlink_tmpfile();
    virtual srs_utime_t get_start_dts();
    virtual srs_error_t unlink_file();
};

// The FMP4(Fragmented MP4) for DASH streaming.
class ISrsFragmentedMp4 : public ISrsFragment
{
public:
    ISrsFragmentedMp4();
    virtual ~ISrsFragmentedMp4();

public:
    // Initialize the fragment, create the home dir, open the file.
    virtual srs_error_t initialize(ISrsRequest *r, bool video, int64_t time, ISrsMpdWriter *mpd, uint32_t tid) = 0;
    // Write media message to fragment.
    virtual srs_error_t write(SrsMediaPacket *shared_msg, SrsFormat *format) = 0;
    // Reap the fragment, close the fd and rename tmp to official file.
    virtual srs_error_t reap(uint64_t &dts) = 0;
};

// The FMP4(Fragmented MP4) for DASH streaming.
class SrsFragmentedMp4 : public ISrsFragmentedMp4
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFileWriter *fw_;
    ISrsMp4M2tsSegmentEncoder *enc_;
    ISrsFragment *fragment_;

public:
    SrsFragmentedMp4();
    virtual ~SrsFragmentedMp4();

public:
    // Initialize the fragment, create the home dir, open the file.
    virtual srs_error_t initialize(ISrsRequest *r, bool video, int64_t time, ISrsMpdWriter *mpd, uint32_t tid);
    // Write media message to fragment.
    virtual srs_error_t write(SrsMediaPacket *shared_msg, SrsFormat *format);
    // Reap the fragment, close the fd and rename tmp to official file.
    virtual srs_error_t reap(uint64_t &dts);

public:
    // ISrsFragment interface implementations - delegate to fragment_
    virtual void set_path(std::string v);
    virtual std::string tmppath();
    virtual srs_error_t rename();
    virtual void append(int64_t dts);
    virtual srs_error_t create_dir();
    virtual void set_number(uint64_t n);
    virtual uint64_t number();
    virtual srs_utime_t duration();
    virtual srs_error_t unlink_tmpfile();
    virtual srs_utime_t get_start_dts();
    virtual srs_error_t unlink_file();
};

// The writer to write MPD for DASH.
class ISrsMpdWriter
{
public:
    ISrsMpdWriter();
    virtual ~ISrsMpdWriter();

public:
    virtual void dispose() = 0;

public:
    virtual srs_error_t initialize(ISrsRequest *r) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;
    // Write MPD according to parsed format of stream.
    virtual srs_error_t write(SrsFormat *format, ISrsFragmentWindow *afragments, ISrsFragmentWindow *vfragments) = 0;

public:
    // Get the fragment relative home and filename.
    // The basetime is the absolute time in srs_utime_t, while the sn(sequence number) is basetime/fragment.
    virtual srs_error_t get_fragment(bool video, std::string &home, std::string &filename, int64_t time, int64_t &sn) = 0;
    // Set the availabilityStartTime once, map the timestamp in media to utc time.
    virtual void set_availability_start_time(srs_utime_t t) = 0;
    virtual srs_utime_t get_availability_start_time() = 0;
};

// The writer to write MPD for DASH.
class SrsMpdWriter : public ISrsMpdWriter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The duration of fragment in srs_utime_t.
    srs_utime_t fragment_;
    // The period to update the mpd in srs_utime_t.
    srs_utime_t update_period_;
    // The timeshift buffer depth in srs_utime_t.
    srs_utime_t timeshit_;
    // The base or home dir for dash to write files.
    std::string home_;
    // The MPD path template, from which to build the file path.
    std::string mpd_file_;
    // The number of fragments in MPD file.
    int window_size_;
    // The availabilityStartTime in MPD file.
    srs_utime_t availability_start_time_;
    // The number of current video segment.
    uint64_t video_number_;
    // The number of current audio segment.
    uint64_t audio_number_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The home for fragment, relative to home.
    std::string fragment_home_;

public:
    SrsMpdWriter();
    virtual ~SrsMpdWriter();

public:
    virtual void dispose();

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    // Write MPD according to parsed format of stream.
    virtual srs_error_t write(SrsFormat *format, ISrsFragmentWindow *afragments, ISrsFragmentWindow *vfragments);

public:
    // Get the fragment relative home and filename.
    // The basetime is the absolute time in srs_utime_t, while the sn(sequence number) is basetime/fragment.
    virtual srs_error_t get_fragment(bool video, std::string &home, std::string &filename, int64_t time, int64_t &sn);
    // Set the availabilityStartTime once, map the timestamp in media to utc time.
    virtual void set_availability_start_time(srs_utime_t t);
    virtual srs_utime_t get_availability_start_time();
};

// The DASH controller interface.
class ISrsDashController
{
public:
    ISrsDashController();
    virtual ~ISrsDashController();

public:
    virtual void dispose() = 0;

public:
    virtual srs_error_t initialize(ISrsRequest *r) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format) = 0;
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format) = 0;
};

// The controller for DASH, control the MPD and FMP4 generating system.
class SrsDashController : public ISrsDashController
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    SrsFormat *format_;
    ISrsMpdWriter *mpd_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFragmentedMp4 *vcurrent_;
    ISrsFragmentWindow *vfragments_;
    ISrsFragmentedMp4 *acurrent_;
    ISrsFragmentWindow *afragments_;
    // Current audio dts.
    uint64_t audio_dts_;
    // Current video dts.
    uint64_t video_dts_;
    // First dts of the stream, use to calculate the availabilityStartTime in MPD.
    int64_t first_dts_;
    // Had the video reaped, use to align audio/video segment's timestamp.
    bool video_reaped_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The fragment duration in srs_utime_t to reap it.
    srs_utime_t fragment_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string home_;
    int video_track_id_;
    int audio_track_id_;

public:
    SrsDashController();
    virtual ~SrsDashController();

public:
    virtual void dispose();

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t refresh_mpd(SrsFormat *format);
    virtual srs_error_t refresh_init_mp4(SrsMediaPacket *msg, SrsFormat *format);
};

// The DASH interface.
class ISrsDash
{
public:
    ISrsDash();
    virtual ~ISrsDash();

public:
    virtual void dispose() = 0;
    virtual srs_error_t cycle() = 0;
    virtual srs_utime_t cleanup_delay() = 0;

public:
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsRequest *r) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format) = 0;
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format) = 0;
    virtual void on_unpublish() = 0;
};

// The MPEG-DASH encoder, transmux RTMP to DASH.
class SrsDash : public ISrsDash
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool enabled_;
    bool disposable_;
    srs_utime_t last_update_time_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    ISrsOriginHub *hub_;
    ISrsDashController *controller_;

public:
    SrsDash();
    virtual ~SrsDash();

public:
    virtual void dispose();
    virtual srs_error_t cycle();
    srs_utime_t cleanup_delay();

public:
    // Initalize the encoder.
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsRequest *r);
    // When stream start publishing.
    virtual srs_error_t on_publish();
    // When got an shared audio message.
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    // When got an shared video message.
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);
    // When stream stop publishing.
    virtual void on_unpublish();
};

#endif
