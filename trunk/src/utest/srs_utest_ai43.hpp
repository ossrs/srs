//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI43_HPP
#define SRS_UTEST_AI43_HPP

/*
#include <srs_utest_ai43.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_hls.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_mock.hpp>

#include <stdio.h>
#include <string>
#include <vector>

extern srs_fopen_t _srs_fopen_fn;
extern srs_fwrite_t _srs_fwrite_fn;
extern srs_mkdir_t _srs_mkdir_fn;
extern srs_open_t _srs_open_fn;
extern srs_read_t _srs_read_fn;

// Removes a dir and everything in it, as an operator's cleanup job does.
extern void hls_utest_remove_all(const std::string &dir);
extern std::string hls_utest_read_file(const std::string &path);
extern void hls_utest_write_file(const std::string &path, const std::string &content);
// The number of times sub is in s.
extern int hls_utest_count(const std::string &s, const std::string &sub);
// The EXTINF durations of a playlist, in ms.
extern std::vector<int> hls_utest_extinf_ms(const std::string &m3u8);

extern bool hls_volume_read_only_;
extern bool hls_volume_full_;
extern std::string hls_volume_read_only_at_;
extern std::string hls_volume_full_at_;
extern bool hls_volume_unreadable_;
extern bool hls_volume_bad_sectors_;
extern FILE *hls_utest_fopen(const char *path, const char *mode);
extern size_t hls_utest_fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream);
extern int hls_utest_mkdir(const char *path);
extern int hls_utest_open(const char *path, int oflag, ...);
extern ssize_t hls_utest_read(int fd, void *buf, size_t count);


// The volume HLS writes to, which a test remounts read-only, fills, or makes fail at the file it names: the volume is
// remounted read-only, or fills, when the server opens that file for writing. A read-only volume creates no file and
// no dir. It can also deny reading, as for a file of another user, or have bad sectors, so reads fail.
class HlsUtestVolumeScope
{
public:
    srs_fopen_t fopen_fn_;
    srs_fwrite_t fwrite_fn_;
    srs_mkdir_t mkdir_fn_;
    srs_open_t open_fn_;
    srs_read_t read_fn_;

public:
    HlsUtestVolumeScope()
    {
        fopen_fn_ = _srs_fopen_fn;
        fwrite_fn_ = _srs_fwrite_fn;
        mkdir_fn_ = _srs_mkdir_fn;
        open_fn_ = _srs_open_fn;
        read_fn_ = _srs_read_fn;
        _srs_fopen_fn = hls_utest_fopen;
        _srs_fwrite_fn = hls_utest_fwrite;
        _srs_mkdir_fn = hls_utest_mkdir;
        _srs_open_fn = hls_utest_open;
        _srs_read_fn = hls_utest_read;
        reset();
    }
    virtual ~HlsUtestVolumeScope()
    {
        _srs_fopen_fn = fopen_fn_;
        _srs_fwrite_fn = fwrite_fn_;
        _srs_mkdir_fn = mkdir_fn_;
        _srs_open_fn = open_fn_;
        _srs_read_fn = read_fn_;
        reset();
    }
    void read_only()
    {
        hls_volume_read_only_ = true;
    }
    void fill()
    {
        hls_volume_full_ = true;
    }
    // Remounts the volume read-only when the server opens the file whose path ends with name for writing.
    void read_only_at(std::string name)
    {
        hls_volume_read_only_at_ = name;
    }
    // Fills the disk when the server opens the file whose path ends with name for writing.
    void fill_at(std::string name)
    {
        hls_volume_full_at_ = name;
    }
    void unreadable()
    {
        hls_volume_unreadable_ = true;
    }
    void bad_sectors()
    {
        hls_volume_bad_sectors_ = true;
    }

private:
    void reset()
    {
        hls_volume_read_only_ = hls_volume_full_ = false;
        hls_volume_read_only_at_ = hls_volume_full_at_ = "";
        hls_volume_unreadable_ = hls_volume_bad_sectors_ = false;
    }
};


// A path that fails every delete, as on a volume remounted read-only, and notes each path it was asked to delete.
class HlsUtestReadOnlyPath : public SrsPath
{
public:
    std::vector<std::string> *unlinks_;

public:
    HlsUtestReadOnlyPath(std::vector<std::string> *unlinks)
    {
        unlinks_ = unlinks;
    }
    virtual srs_error_t unlink(std::string path)
    {
        unlinks_->push_back(path);
        return srs_error_new(ERROR_SYSTEM_FILE_UNLINK, "unlink %s, read-only file system", path.c_str());
    }
};

// The server's factory, but its paths fail every delete.
class HlsUtestReadOnlyFactory : public SrsAppFactory
{
public:
    std::vector<std::string> unlinks_;

public:
    virtual SrsPath *create_path()
    {
        return new HlsUtestReadOnlyPath(&unlinks_);
    }
};

// A stream published with HLS on, its files under home, fed the packets of an RTMP publisher as the origin hub feeds
// them: each packet is parsed by the stream's format first. As the hub, it caches the sequence headers and feeds them
// again when HLS asks, after a reload or when a disposed HLS resumes.
class HlsUtestStream : public MockOriginHub
{
public:
    string home_;
    MockSrsConfig conf_;
    MockSrsRequest req_;
    SrsHls *hls_;
    SrsFormat format_;
    SrsMediaPacket *vsh_;
    SrsFormat vsh_format_;
    SrsMediaPacket *ash_;
    SrsFormat ash_format_;
    // The factory of the muxer's files and paths, or NULL for the server's.
    ISrsAppFactory *factory_;

public:
    HlsUtestStream(std::string name) : req_("__defaultVhost__", "live", "livestream")
    {
        home_ = _srs_tmp_file_prefix + name;
        hls_utest_remove_all(home_);
        hls_ = new SrsHls();
        hls_->assemble();
        vsh_ = ash_ = NULL;
        factory_ = NULL;
    }
    virtual ~HlsUtestStream()
    {
        srs_freep(hls_);
        srs_freep(vsh_);
        srs_freep(ash_);
        hls_utest_remove_all(home_);
    }

public:
    // Loads the config of the vhost, whose hls section has these options, and hls_path under home. HLS is on, unless
    // the options turn it off.
    srs_error_t load(MockSrsConfig &conf, string options)
    {
        return conf.mock_parse(string(_MIN_OK_CONF) + "vhost __defaultVhost__ { hls { " + options + " hls_path " + home_ + "; enabled on; } }");
    }
    // Points HLS, its controller and muxer at this config, and the muxer at the test's factory, if any.
    void use(ISrsAppConfig *conf)
    {
        hls_->config_ = conf;
        SrsHlsController *ts = dynamic_cast<SrsHlsController *>(hls_->controller_);
        if (ts) {
            ts->config_ = conf;
            SrsHlsMuxer *muxer = dynamic_cast<SrsHlsMuxer *>(ts->muxer_);
            muxer->config_ = conf;
            if (factory_) {
                muxer->app_factory_ = factory_;
            }
        }
        SrsHlsMp4Controller *mp4 = dynamic_cast<SrsHlsMp4Controller *>(hls_->controller_);
        if (mp4) {
            mp4->config_ = conf;
            mp4->muxer_->config_ = conf;
            if (factory_) {
                mp4->muxer_->app_factory_ = factory_;
            }
        }
    }
    srs_error_t publish(string options)
    {
        srs_error_t err = srs_success;

        if ((err = load(conf_, options)) != srs_success) {
            return srs_error_wrap(err, "config");
        }
        if ((err = format_.initialize()) != srs_success) {
            return srs_error_wrap(err, "format");
        }
        if ((err = vsh_format_.initialize()) != srs_success) {
            return srs_error_wrap(err, "vsh format");
        }
        if ((err = ash_format_.initialize()) != srs_success) {
            return srs_error_wrap(err, "ash format");
        }

        hls_->config_ = &conf_;
        if ((err = hls_->initialize(this, &req_)) != srs_success) {
            return srs_error_wrap(err, "initialize");
        }
        use(&conf_);

        return hls_->on_publish();
    }
    srs_error_t video(int64_t ts, const uint8_t *data, int size)
    {
        srs_error_t err = srs_success;

        SrsMediaPacket msg;
        char *payload = new char[size];
        memcpy(payload, data, size);
        msg.wrap(payload, size);
        msg.timestamp_ = ts;
        msg.message_type_ = SrsFrameTypeVideo;

        if ((err = format_.on_video(ts, msg.payload(), msg.size())) != srs_success) {
            return srs_error_wrap(err, "format");
        }
        if (format_.is_avc_sequence_header()) {
            srs_freep(vsh_);
            vsh_ = msg.copy();
            if ((err = vsh_format_.on_video(ts, vsh_->payload(), vsh_->size())) != srs_success) {
                return srs_error_wrap(err, "vsh format");
            }
        }

        return hls_->on_video(&msg, &format_);
    }
    srs_error_t audio(int64_t ts, const uint8_t *data, int size)
    {
        srs_error_t err = srs_success;

        SrsMediaPacket msg;
        char *payload = new char[size];
        memcpy(payload, data, size);
        msg.wrap(payload, size);
        msg.timestamp_ = ts;
        msg.message_type_ = SrsFrameTypeAudio;

        if ((err = format_.on_audio(ts, msg.payload(), msg.size())) != srs_success) {
            return srs_error_wrap(err, "format");
        }
        if (format_.is_aac_sequence_header()) {
            srs_freep(ash_);
            ash_ = msg.copy();
            if ((err = ash_format_.on_audio(ts, ash_->payload(), ash_->size())) != srs_success) {
                return srs_error_wrap(err, "ash format");
            }
        }

        return hls_->on_audio(&msg, &format_);
    }
    // Sends the sequence headers of both tracks.
    srs_error_t sequence_headers()
    {
        srs_error_t err = srs_success;

        if ((err = video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh))) != srs_success) {
            return srs_error_wrap(err, "avc sh");
        }
        if ((err = audio(0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh))) != srs_success) {
            return srs_error_wrap(err, "aac sh");
        }
        return err;
    }
    // Sends the media from one time to another, in ms: 10 video and 10 audio frames a second, a key frame each second.
    srs_error_t av(int64_t from, int64_t to)
    {
        srs_error_t err = srs_success;

        for (int64_t ts = from; ts <= to; ts += 100) {
            const uint8_t *frame = (ts % 1000) == 0 ? flv_utest_avc_idr : flv_utest_avc_inter;
            if ((err = video(ts, frame, sizeof(flv_utest_avc_idr))) != srs_success) {
                return srs_error_wrap(err, "video %d", (int)ts);
            }
            if ((err = audio(ts, flv_utest_aac_frame, sizeof(flv_utest_aac_frame))) != srs_success) {
                return srs_error_wrap(err, "audio %d", (int)ts);
            }
        }
        return err;
    }
    // On HLS's request, feeds the cached sequence headers again, as SrsOriginHub does.
    virtual srs_error_t on_hls_request_sh()
    {
        srs_error_t err = srs_success;

        on_hls_request_sh_count_++;
        if (on_hls_request_sh_error_) {
            return srs_error_copy(on_hls_request_sh_error_);
        }

        if (vsh_ && (err = hls_->on_video(vsh_, &vsh_format_)) != srs_success) {
            return srs_error_wrap(err, "hls video sh");
        }
        if (ash_ && (err = hls_->on_audio(ash_, &ash_format_)) != srs_success) {
            return srs_error_wrap(err, "hls audio sh");
        }
        return err;
    }

public:
    string path(std::string name)
    {
        return home_ + "/live/" + name;
    }
    bool exists(std::string name)
    {
        SrsPath p;
        return p.exists(path(name));
    }
    string m3u8()
    {
        return hls_utest_read_file(path("livestream.m3u8"));
    }
};

#endif
