//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_DASH_HPP
#define SRS_APP_DASH_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_app_fragment.hpp>

class SrsRequest;
class SrsOriginHub;
class SrsSharedPtrMessage;
class SrsFormat;
class SrsFileWriter;
class SrsMpdWriter;
class SrsMp4M2tsInitEncoder;
class SrsMp4M2tsSegmentEncoder;

// The init mp4 for FMP4.
class SrsInitMp4 : public SrsFragment
{
private:
    SrsFileWriter* fw;
    SrsMp4M2tsInitEncoder* init;
public:
    SrsInitMp4();
    virtual ~SrsInitMp4();
public:
    // Write the init mp4 file, with the tid(track id).
    virtual srs_error_t write(SrsFormat* format, bool video, int tid);
};

// The FMP4(Fragmented MP4) for DASH streaming.
class SrsFragmentedMp4 : public SrsFragment
{
private:
    SrsFileWriter* fw;
    SrsMp4M2tsSegmentEncoder* enc;
public:
    SrsFragmentedMp4();
    virtual ~SrsFragmentedMp4();
public:
    // Initialize the fragment, create the home dir, open the file.
    virtual srs_error_t initialize(SrsRequest* r, bool video, int64_t time, SrsMpdWriter* mpd, uint32_t tid);
    // Write media message to fragment.
    virtual srs_error_t write(SrsSharedPtrMessage* shared_msg, SrsFormat* format);
    // Reap the fragment, close the fd and rename tmp to official file.
    virtual srs_error_t reap(uint64_t& dts);
};

// The writer to write MPD for DASH.
class SrsMpdWriter
{
private:
    SrsRequest* req;
private:
    // The duration of fragment in srs_utime_t.
    srs_utime_t fragment;
    // The period to update the mpd in srs_utime_t.
    srs_utime_t update_period;
    // The timeshift buffer depth in srs_utime_t.
    srs_utime_t timeshit;
    // The base or home dir for dash to write files.
    std::string home;
    // The MPD path template, from which to build the file path.
    std::string mpd_file;
    // The number of fragments in MPD file.
    int window_size_;
    // The availabilityStartTime in MPD file.
    srs_utime_t availability_start_time_;
    // The number of current video segment.
    uint64_t video_number_;
    // The number of current audio segment.
    uint64_t audio_number_;
private:
    // The home for fragment, relative to home.
    std::string fragment_home;
public:
    SrsMpdWriter();
    virtual ~SrsMpdWriter();
public:
    virtual void dispose();
public:
    virtual srs_error_t initialize(SrsRequest* r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    // Write MPD according to parsed format of stream.
    virtual srs_error_t write(SrsFormat* format, SrsFragmentWindow* afragments, SrsFragmentWindow* vfragments);
public:
    // Get the fragment relative home and filename.
    // The basetime is the absolute time in srs_utime_t, while the sn(sequence number) is basetime/fragment.
    virtual srs_error_t get_fragment(bool video, std::string& home, std::string& filename, int64_t time, int64_t& sn);
    // Set the availabilityStartTime once, map the timestamp in media to utc time.
    virtual void set_availability_start_time(srs_utime_t t);
    virtual srs_utime_t get_availability_start_time();
};

// The controller for DASH, control the MPD and FMP4 generating system.
class SrsDashController
{
private:
    SrsRequest* req;
    SrsFormat* format_;
    SrsMpdWriter* mpd;
private:
    SrsFragmentedMp4* vcurrent;
    SrsFragmentWindow* vfragments;
    SrsFragmentedMp4* acurrent;
    SrsFragmentWindow* afragments;
    // Current audio dts.
    uint64_t audio_dts;
    // Current video dts.
    uint64_t video_dts;
    // First dts of the stream, use to calculate the availabilityStartTime in MPD.
    int64_t first_dts_;
    // Had the video reaped, use to align audio/video segment's timestamp.
    bool video_reaped_;
private:
    // The fragment duration in srs_utime_t to reap it.
    srs_utime_t fragment;
private:
    std::string home;
    int video_track_id;
    int audio_track_id;
public:
    SrsDashController();
    virtual ~SrsDashController();
public:
    virtual void dispose();
public:
    virtual srs_error_t initialize(SrsRequest* r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format);
    virtual srs_error_t on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format);
private:
    virtual srs_error_t refresh_mpd(SrsFormat* format);
    virtual srs_error_t refresh_init_mp4(SrsSharedPtrMessage* msg, SrsFormat* format);
};

// The MPEG-DASH encoder, transmux RTMP to DASH.
class SrsDash
{
private:
    bool enabled;
    bool disposable_;
    srs_utime_t last_update_time_;
private:
    SrsRequest* req;
    SrsOriginHub* hub;
    SrsDashController* controller;
public:
    SrsDash();
    virtual ~SrsDash();
public:
    virtual void dispose();
    virtual srs_error_t cycle();
public:
    // Initalize the encoder.
    virtual srs_error_t initialize(SrsOriginHub* h, SrsRequest* r);
    // When stream start publishing.
    virtual srs_error_t on_publish();
    // When got an shared audio message.
    virtual srs_error_t on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format);
    // When got an shared video message.
    virtual srs_error_t on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format);
    // When stream stop publishing.
    virtual void on_unpublish();
};

#endif

