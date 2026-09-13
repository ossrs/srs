//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_hds.hpp>

#ifdef SRS_HDS

#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_hds.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>

static void update_box(char *start, int size)
{
    char *p_size = (char *)&size;
    start[0] = p_size[3];
    start[1] = p_size[2];
    start[2] = p_size[1];
    start[3] = p_size[0];
}

char flv_header[] = {'F', 'L', 'V',
                     0x01, 0x05, 0x00, 0x00, 0x00, 0x09,
                     0x00, 0x00, 0x00, 0x00};

string serialFlv(SrsMediaPacket *msg)
{
    int size = 15 + msg->size();
    char *byte = new char[size];

    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(byte, size));

    // tag header
    long long dts = msg->timestamp_;
    char type = msg->is_video() ? 0x09 : 0x08;

    stream->write_1bytes(type);
    stream->write_3bytes(msg->size());
    stream->write_3bytes((int32_t)dts);
    stream->write_1bytes(dts >> 24 & 0xFF);
    stream->write_3bytes(0);
    stream->write_bytes(msg->payload(), msg->size());

    // pre tag size
    int preTagSize = msg->size() + 11;
    stream->write_4bytes(preTagSize);

    string ret(stream->data(), stream->size());

    delete[] byte;

    return ret;
}

class SrsHdsFragment
{
public:
    SrsHdsFragment(ISrsRequest *r)
        : req_(r), index_(-1), start_time_(0), video_sh_(NULL), audio_sh_(NULL)
    {
    }

    ~SrsHdsFragment()
    {
        srs_freep(video_sh_);
        srs_freep(audio_sh_);

        // clean msgs
        list<SrsMediaPacket *>::iterator iter;
        for (iter = msgs_.begin(); iter != msgs_.end(); ++iter) {
            SrsMediaPacket *msg = *iter;
            srs_freep(msg);
        }
    }

    void on_video(SrsMediaPacket *msg)
    {
        SrsMediaPacket *_msg = msg->copy();
        msgs_.push_back(_msg);
    }

    void on_audio(SrsMediaPacket *msg)
    {
        SrsMediaPacket *_msg = msg->copy();
        msgs_.push_back(_msg);
    }

    /*!
     flush data to disk.
     */
    srs_error_t flush()
    {
        string data;
        if (video_sh_) {
            video_sh_->timestamp_ = start_time_;
            data.append(serialFlv(video_sh_));
        }

        if (audio_sh_) {
            audio_sh_->timestamp_ = start_time_;
            data.append(serialFlv(audio_sh_));
        }

        list<SrsMediaPacket *>::iterator iter;
        for (iter = msgs_.begin(); iter != msgs_.end(); ++iter) {
            SrsMediaPacket *msg = *iter;
            data.append(serialFlv(msg));
        }

        char box_header[8];
        SrsBuffer ss(box_header, 8);
        ss.write_4bytes((int32_t)(8 + data.size()));
        ss.write_string("mdat");

        data = string(ss.data(), ss.size()) + data;

        const char *file_path = path_.c_str();
        int fd = open(file_path, O_WRONLY | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
        if (fd < 0) {
            return srs_error_new(-1, "open fragment file failed, path=%s", file_path);
        }

        if (write(fd, data.data(), data.size()) != (int)data.size()) {
            close(fd);
            return srs_error_new(-1, "write fragment file failed, path=", file_path);
        }
        close(fd);

        srs_trace("build fragment success=%s", file_path);

        return srs_success;
    }

    /*!
     calc the segment duration in milliseconds.
     @return 0 if no msgs
     or the last msg dts minus the first msg dts.
     */
    int duration()
    {
        int duration_ms = 0;
        long long first_msg_ts = 0;
        long long last_msg_ts = 0;

        if (msgs_.size() >= 2) {
            SrsMediaPacket *first_msg = msgs_.front();
            first_msg_ts = first_msg->timestamp_;

            SrsMediaPacket *last_msg = msgs_.back();
            last_msg_ts = last_msg->timestamp_;

            duration_ms = (int)(last_msg_ts - first_msg_ts);
        }

        return duration_ms;
    }

    /*!
     set/get index
     */
    inline void set_index(int idx)
    {
        char file_path[1024] = {0};
        snprintf(file_path, 1024, "%s/%s/%sSeg1-Frag%d", _srs_config->get_hds_path(req_->vhost_).c_str(), req_->app_.c_str(), req_->stream_.c_str(), idx);

        path_ = file_path;
        index_ = idx;
    }

    inline int get_index()
    {
        return index_;
    }

    /*!
     set/get start time
     */
    inline void set_start_time(long long st)
    {
        start_time_ = st;
    }

    inline long long get_start_time()
    {
        return start_time_;
    }

    void set_video_sh(SrsMediaPacket *msg)
    {
        srs_freep(video_sh_);
        video_sh_ = msg->copy();
    }

    void set_audio_sh(SrsMediaPacket *msg)
    {
        srs_freep(audio_sh_);
        audio_sh_ = msg->copy();
    }

    string fragment_path()
    {
        return path_;
    }

private:
    ISrsRequest *req_;
    list<SrsMediaPacket *> msgs_;

    /*!
     the index of this fragment
     */
    int index_;
    long long start_time_;

    SrsMediaPacket *video_sh_;
    SrsMediaPacket *audio_sh_;
    string path_;
};

ISrsHds::ISrsHds()
{
}

ISrsHds::~ISrsHds()
{
}

SrsHds::SrsHds()
    : currentSegment_(NULL), fragment_index_(1), video_sh_(NULL), audio_sh_(NULL), hds_req_(NULL), hds_enabled_(false)
{
}

SrsHds::~SrsHds()
{
}

srs_error_t SrsHds::on_publish(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    if (hds_enabled_) {
        return err;
    }

    std::string vhost = req->vhost_;
    if (!_srs_config->get_hds_enabled(vhost)) {
        hds_enabled_ = false;
        return err;
    }
    hds_enabled_ = true;

    hds_req_ = req->copy();

    return flush_mainfest();
}

srs_error_t SrsHds::on_unpublish()
{
    srs_error_t err = srs_success;

    if (!hds_enabled_) {
        return err;
    }

    hds_enabled_ = false;

    srs_freep(video_sh_);
    srs_freep(audio_sh_);
    srs_freep(hds_req_);

    // clean fragments
    list<SrsHdsFragment *>::iterator iter;
    for (iter = fragments_.begin(); iter != fragments_.end(); ++iter) {
        SrsHdsFragment *st = *iter;
        srs_freep(st);
    }
    fragments_.clear();

    srs_freep(currentSegment_);

    srs_trace("HDS un-published");

    return err;
}

srs_error_t SrsHds::on_video(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    if (!hds_enabled_) {
        return err;
    }

    if (SrsFlvVideo::sh(msg->payload(), msg->size())) {
        srs_freep(video_sh_);
        video_sh_ = msg->copy();
    }

    if (!currentSegment_) {
        currentSegment_ = new SrsHdsFragment(hds_req_);
        currentSegment_->set_index(fragment_index_++);
        currentSegment_->set_start_time(msg->timestamp_);

        if (video_sh_)
            currentSegment_->set_video_sh(video_sh_);

        if (audio_sh_)
            currentSegment_->set_audio_sh(audio_sh_);
    }

    currentSegment_->on_video(msg);

    double fragment_duration = srsu2ms(_srs_config->get_hds_fragment(hds_req_->vhost_));
    if (currentSegment_->duration() >= fragment_duration) {
        // flush segment
        if ((err = currentSegment_->flush()) != srs_success) {
            return srs_error_wrap(err, "flush segment");
        }

        srs_trace("flush Segment success.");
        fragments_.push_back(currentSegment_);
        currentSegment_ = NULL;
        adjust_windows();

        // flush bootstrap
        if ((err = flush_bootstrap()) != srs_success) {
            return srs_error_wrap(err, "flush bootstrap");
        }

        srs_trace("flush BootStrap success.");
    }

    return err;
}

srs_error_t SrsHds::on_audio(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    if (!hds_enabled_) {
        return err;
    }

    if (SrsFlvAudio::sh(msg->payload(), msg->size())) {
        srs_freep(audio_sh_);
        audio_sh_ = msg->copy();
    }

    if (!currentSegment_) {
        currentSegment_ = new SrsHdsFragment(hds_req_);
        currentSegment_->set_index(fragment_index_++);
        currentSegment_->set_start_time(msg->timestamp_);

        if (video_sh_)
            currentSegment_->set_video_sh(video_sh_);

        if (audio_sh_)
            currentSegment_->set_audio_sh(audio_sh_);
    }

    currentSegment_->on_audio(msg);

    double fragment_duration = srsu2ms(_srs_config->get_hds_fragment(hds_req_->vhost_));
    if (currentSegment_->duration() >= fragment_duration) {
        // flush segment
        if ((err = currentSegment_->flush()) != srs_success) {
            return srs_error_wrap(err, "flush segment");
        }

        srs_info("flush Segment success.");

        // reset the current segment
        fragments_.push_back(currentSegment_);
        currentSegment_ = NULL;
        adjust_windows();

        // flush bootstrap
        if ((err = flush_bootstrap()) != srs_success) {
            return srs_error_wrap(err, "flush bootstrap");
        }

        srs_info("flush BootStrap success.");
    }

    return err;
}

srs_error_t SrsHds::flush_mainfest()
{
    srs_error_t err = srs_success;

    char buf[1024] = {0};
    snprintf(buf, 1024, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                        "<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n\t"
                        "<id>%s.f4m</id>\n\t"
                        "<streamType>live</streamType>\n\t"
                        "<deliveryType>streaming</deliveryType>\n\t"
                        "<bootstrapInfo profile=\"named\" url=\"%s.abst\" id=\"bootstrap0\" />\n\t"
                        "<media bitrate=\"0\" url=\"%s\" bootstrapInfoId=\"bootstrap0\"></media>\n"
                        "</manifest>",
             hds_req_->stream_.c_str(), hds_req_->stream_.c_str(), hds_req_->stream_.c_str());

    SrsPath path_util;
    string dir = _srs_config->get_hds_path(hds_req_->vhost_) + "/" + hds_req_->app_;
    if ((err = path_util.mkdir_all(dir)) != srs_success) {
        return srs_error_wrap(err, "hds create dir failed");
    }
    string path = dir + "/" + hds_req_->stream_ + ".f4m";

    int fd = open(path.c_str(), O_WRONLY | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
    if (fd < 0) {
        return srs_error_new(ERROR_HDS_OPEN_F4M_FAILED, "open manifest file failed, path=%s", path.c_str());
    }

    int f4m_size = (int)strlen(buf);
    if (write(fd, buf, f4m_size) != f4m_size) {
        close(fd);
        return srs_error_new(ERROR_HDS_WRITE_F4M_FAILED, "write manifest file failed, path=", path.c_str());
    }
    close(fd);

    srs_trace("build manifest success=%s", path.c_str());

    return err;
}

srs_error_t SrsHds::flush_bootstrap()
{
    srs_error_t err = srs_success;

    int size = 1024 * 100;

    SrsUniquePtr<char[]> start_abst(new char[1024 * 100]);

    int size_abst = 0;
    char *start_asrt = NULL;
    int size_asrt = 0;
    char *start_afrt = NULL;
    int size_afrt = 0;

    SrsBuffer abst(start_abst.get(), size);

    // @see video_file_format_spec_v10_1
    // page: 46
    abst.write_4bytes(0);
    abst.write_string("abst");
    abst.write_1bytes(0x00); // Either 0 or 1
    abst.write_3bytes(0x00); // Flags always 0
    size_abst += 12;
    /*!
     @BootstrapinfoVersion       UI32
     The version number of the bootstrap information.
     When the Update field is set, BootstrapinfoVersion
     indicates the version number that is being updated.
     we assume this is the last.
     */
    abst.write_4bytes(fragment_index_ - 1); // BootstrapinfoVersion

    abst.write_1bytes(0x20); // profile, live, update
    abst.write_4bytes(1000); // TimeScale Typically, the value is 1000, for a unit of milliseconds
    size_abst += 9;
    /*!
     The timestamp in TimeScale units of the latest available Fragment in the media presentation.
     This timestamp is used to request the right fragment number.
     The CurrentMedia Time can be the total duration.
     For media presentations that are not live, CurrentMediaTime can be 0.
     */
    SrsHdsFragment *st = fragments_.back();
    abst.write_8bytes(st->get_start_time());

    // SmpteTimeCodeOffset
    abst.write_8bytes(0);
    size_abst += 16;

    /*!
     @MovieIdentifier        STRING
     The identifier of this presentation.
     we write null string.
     */
    abst.write_1bytes(0);
    size_abst += 1;
    /*!
     @ServerEntryCount       UI8
     The number of ServerEntryTable entries.
     The minimum value is 0.
     */
    abst.write_1bytes(0);
    size_abst += 1;
    /*!
     @ServerEntryTable
     because we write 0 of ServerEntryCount, so this feild is ignored.
     */

    /*!
     @QualityEntryCount      UI8
     The number of QualityEntryTable entries, which is
     also the number of available quality levels. The
     minimum value is 0. Available quality levels are for,
     for example, multi bit rate files or trick files.
     */
    abst.write_1bytes(0);
    size_abst += 1;
    /*!
     @QualityEntryTable
     because we write 0 of QualityEntryCount, so this feild is ignored.
     */

    /*!
     @DrmData        STRING
     Null or null-terminated UTF-8 string.  This string
     holds Digital Rights Management metadata.
     Encrypted files use this metadata to get the
     necessary keys and licenses for decryption and play back.
     we write null string.
     */
    abst.write_1bytes(0);
    size_abst += 1;
    /*!
     @MetaData       STRING
     Null or null-terminated UTF - 8 string that holds metadata.
     we write null string.
     */
    abst.write_1bytes(0);
    size_abst += 1;
    /*!
     @SegmentRunTableCount       UI8
     The number of entries in SegmentRunTableEntries.
     The minimum value is 1. Typically, one table
     contains all segment runs. However, this count
     provides the flexibility to define the segment runs
     individually for each quality level (or trick file).
     */
    abst.write_1bytes(1);
    size_abst += 1;

    start_asrt = start_abst.get() + size_abst;

    // follows by asrt
    abst.write_4bytes(0);
    abst.write_string("asrt");
    size_asrt += 8;
    /*!
     @Version        UI8
     @Flags          UI24
     */
    abst.write_4bytes(0);
    size_asrt += 4;
    /*!
     @QualityEntryCount      UI8
     The number of QualitySegmen tUrlModifiers
     (quality level references) that follow. If 0, this
     Segment Run Table applies to all quality levels,
     and there shall be only one Segment Run Table
     box in the Bootstrap Info box.
     */
    abst.write_1bytes(0);
    size_asrt += 1;

    /*!
     @QualitySegmentUrlModifiers
     ignored.
     */

    /*!
     @SegmentRunEntryCount
     The number of items in this
     SegmentRunEn tryTable. The minimum value is 1.
     */
    abst.write_4bytes(1);
    size_asrt += 4;
    /*!
     @SegmentRunEntryTable
     */
    for (int i = 0; i < 1; ++i) {
        /*!
         @FirstSegment       UI32
         The identifying number of the first segment in the run of
         segments containing the same number of fragments.
         The segment corresponding to the FirstSegment in the next
         SEGMENTRUNENTRY will terminate this run.
         */
        abst.write_4bytes(1);

        /*!
         @FragmentsPerSegment        UI32
         The number of fragments in each segment in this run.
         */
        abst.write_4bytes(fragment_index_ - 1);
        size_asrt += 8;
    }

    update_box(start_asrt, size_asrt);
    size_abst += size_asrt;

    /*!
     @FragmentRunTableCount      UI8
     The number of entries in FragmentRunTable-Entries.
     The min i mum value is 1.
     */
    abst.write_1bytes(1);
    size_abst += 1;

    // follows by afrt
    start_afrt = start_abst.get() + size_abst;

    abst.write_4bytes(0);
    abst.write_string("afrt");
    size_afrt += 8;

    /*!
     @Version        UI8
     @Flags          UI24
     */
    abst.write_4bytes(0);
    size_afrt += 4;
    /*!
     @TimeScale      UI32
     The number of time units per second, used in the FirstFragmentTime stamp and
     Fragment Duration fields.
     Typically, the value is 1000.
     */
    abst.write_4bytes(1000);
    size_afrt += 4;
    /*!
     @QualityEntryCount      UI8
     The number of QualitySegment Url Modifiers
     (quality level references) that follow.
     If 0, this Fragment Run Table applies to all quality levels,
     and there shall be only one Fragment Run Table
     box in the Bootstrap Info box.
     */
    abst.write_1bytes(0);
    size_afrt += 1;

    /*!
     @FragmentRunEntryCount      UI32
     The number of items in this FragmentRunEntryTable.
     The minimum value is 1.
     */
    abst.write_4bytes((int32_t)fragments_.size());
    size_afrt += 4;

    list<SrsHdsFragment *>::iterator iter;
    for (iter = fragments_.begin(); iter != fragments_.end(); ++iter) {
        SrsHdsFragment *st = *iter;
        abst.write_4bytes(st->get_index());
        abst.write_8bytes(st->get_start_time());
        abst.write_4bytes(st->duration());
        size_afrt += 16;
    }

    update_box(start_afrt, size_afrt);
    size_abst += size_afrt;
    update_box(start_abst.get(), size_abst);

    string path = _srs_config->get_hds_path(hds_req_->vhost_) + "/" + hds_req_->app_ + "/" + hds_req_->stream_ + ".abst";

    int fd = open(path.c_str(), O_WRONLY | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
    if (fd < 0) {
        return srs_error_new(ERROR_HDS_OPEN_BOOTSTRAP_FAILED, "open bootstrap file failed, path=%s", path.c_str());
    }

    if (write(fd, start_abst.get(), size_abst) != size_abst) {
        close(fd);
        return srs_error_new(ERROR_HDS_WRITE_BOOTSTRAP_FAILED, "write bootstrap file failed, path=", path.c_str());
    }
    close(fd);

    srs_trace("build bootstrap success=%s", path.c_str());

    return err;
}

void SrsHds::adjust_windows()
{
    int windows_size = 0;
    list<SrsHdsFragment *>::iterator iter;
    for (iter = fragments_.begin(); iter != fragments_.end(); ++iter) {
        SrsHdsFragment *fragment = *iter;
        windows_size += fragment->duration();
    }

    double windows_size_limit = srsu2ms(_srs_config->get_hds_window(hds_req_->vhost_));
    if (windows_size > windows_size_limit) {
        SrsHdsFragment *fragment = fragments_.front();
        SrsPath path;
        path.unlink(fragment->fragment_path());
        fragments_.erase(fragments_.begin());
        srs_freep(fragment);
    }
}

#endif
