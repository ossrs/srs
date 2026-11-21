//
// Copyright (c) 2013-2026 The SRS Authors
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

class ISrsInitMp4 : public ISrsFragment
{
public:
    ISrsInitMp4();
    virtual ~ISrsInitMp4();

public:
    virtual srs_error_t write(SrsFormat *format, bool video, int tid) = 0;
};

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
    virtual srs_error_t write(SrsFormat *format, bool video, int tid);

public:
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

class ISrsFragmentedMp4 : public ISrsFragment
{
public:
    ISrsFragmentedMp4();
    virtual ~ISrsFragmentedMp4();

public:
    virtual srs_error_t initialize(ISrsRequest *r, bool video, int64_t time, ISrsMpdWriter *mpd, uint32_t tid) = 0;
    virtual srs_error_t write(SrsMediaPacket *shared_msg, SrsFormat *format) = 0;
    virtual srs_error_t reap(uint64_t &dts) = 0;
};

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
    virtual srs_error_t initialize(ISrsRequest *r, bool video, int64_t time, ISrsMpdWriter *mpd, uint32_t tid);
    virtual srs_error_t write(SrsMediaPacket *shared_msg, SrsFormat *format);
    virtual srs_error_t reap(uint64_t &dts);

public:
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
    virtual srs_error_t write(SrsFormat *format, ISrsFragmentWindow *afragments, ISrsFragmentWindow *vfragments) = 0;

public:
    virtual srs_error_t get_fragment(bool video, std::string &home, std::string &filename, int64_t time, int64_t &sn) = 0;
    // The availabilityStartTime of the MPD, which maps the media timestamps to UTC time.
    virtual void set_availability_start_time(srs_utime_t t) = 0;
    virtual srs_utime_t get_availability_start_time() = 0;
};

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
    srs_utime_t fragment_;
    srs_utime_t update_period_;
    srs_utime_t timeshit_;
    std::string home_;
    std::string mpd_file_;
    int window_size_;
    srs_utime_t availability_start_time_;
    uint64_t video_number_;
    uint64_t audio_number_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string fragment_home_;
    std::string mpd_path_;

public:
    SrsMpdWriter();
    virtual ~SrsMpdWriter();

public:
    virtual void dispose();

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t write(SrsFormat *format, ISrsFragmentWindow *afragments, ISrsFragmentWindow *vfragments);

public:
    virtual srs_error_t get_fragment(bool video, std::string &home, std::string &filename, int64_t time, int64_t &sn);
    virtual void set_availability_start_time(srs_utime_t t);
    virtual srs_utime_t get_availability_start_time();
};

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
    uint64_t audio_dts_;
    uint64_t video_dts_;
    int64_t first_dts_;
    bool video_reaped_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_utime_t fragment_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string home_;
    int video_track_id_;
    int audio_track_id_;
    std::string video_init_file_;
    std::string audio_init_file_;

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

class SrsDash : public ISrsDash
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool enabled_;
    bool disposable_;
    bool disposed_publishing_;
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

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t resume();

public:
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);
    virtual void on_unpublish();
};

#endif
