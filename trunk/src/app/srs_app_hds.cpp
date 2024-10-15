//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_hds.hpp>

#ifdef SRS_HDS

#include <unistd.h>
#include <string>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
using namespace std;

#include <srs_app_hds.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_config.hpp>

static void update_box(char *start, int size)
{
    char *p_size = (char*)&size;
    start[0] = p_size[3];
    start[1] = p_size[2];
    start[2] = p_size[1];
    start[3] = p_size[0];
}

char flv_header[] = {'F', 'L', 'V',
    0x01, 0x05, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00};

string serialFlv(SrsSharedPtrMessage *msg)
{
    int size = 15 + msg->size;
    char *byte = new char[size];

    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(byte, size));

    // tag header
    long long dts = msg->timestamp;
    char type = msg->is_video() ? 0x09 : 0x08;
    
    stream->write_1bytes(type);
    stream->write_3bytes(msg->size);
    stream->write_3bytes((int32_t)dts);
    stream->write_1bytes(dts >> 24 & 0xFF);
    stream->write_3bytes(0);
    stream->write_bytes(msg->payload, msg->size);
    
    // pre tag size
    int preTagSize = msg->size + 11;
    stream->write_4bytes(preTagSize);
    
    string ret(stream->data(), stream->size());
    
    delete [] byte;
    
    return ret;
}

class SrsHdsFragment
{
public:
    SrsHdsFragment(SrsRequest *r)
    : req(r)
    , index(-1)
    , start_time(0)
    , videoSh(NULL)
    , audioSh(NULL)
    {
        
    }
    
    ~SrsHdsFragment()
    {
        srs_freep(videoSh);
        srs_freep(audioSh);
        
        // clean msgs
        list<SrsSharedPtrMessage *>::iterator iter;
        for (iter = msgs.begin(); iter != msgs.end(); ++iter) {
            SrsSharedPtrMessage *msg = *iter;
            srs_freep(msg);
        }
    }
    
    void on_video(SrsSharedPtrMessage *msg)
    {
        SrsSharedPtrMessage *_msg = msg->copy();
        msgs.push_back(_msg);
    }
    
    void on_audio(SrsSharedPtrMessage *msg)
    {
        SrsSharedPtrMessage *_msg = msg->copy();
        msgs.push_back(_msg);
    }
    
    /*!
     flush data to disk.
     */
    srs_error_t flush()
    {
        string data;
        if (videoSh) {
            videoSh->timestamp = start_time;
            data.append(serialFlv(videoSh));
        }
        
        if (audioSh) {
            audioSh->timestamp = start_time;
            data.append(serialFlv(audioSh));
        }
        
        list<SrsSharedPtrMessage *>::iterator iter;
        for (iter = msgs.begin(); iter != msgs.end(); ++iter) {
            SrsSharedPtrMessage *msg = *iter;
            data.append(serialFlv(msg));
        }
        
        char box_header[8];
        SrsBuffer ss(box_header, 8);
        ss.write_4bytes((int32_t)(8 + data.size()));
        ss.write_string("mdat");
        
        data = string(ss.data(), ss.size()) + data;
        
        const char *file_path = path.c_str();
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
        
        if (msgs.size() >= 2) {
            SrsSharedPtrMessage *first_msg = msgs.front();
            first_msg_ts = first_msg->timestamp;
            
            SrsSharedPtrMessage *last_msg = msgs.back();
            last_msg_ts = last_msg->timestamp;
            
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
        snprintf(file_path, 1024, "%s/%s/%sSeg1-Frag%d", _srs_config->get_hds_path(req->vhost).c_str()
                , req->app.c_str(), req->stream.c_str(), idx);
        
        path = file_path;
        index = idx;
    }
    
    inline int get_index()
    {
        return index;
    }
    
    /*!
     set/get start time
     */
    inline void set_start_time(long long st)
    {
        start_time = st;
    }
    
    inline long long get_start_time()
    {
        return start_time;
    }
    
    void set_video_sh(SrsSharedPtrMessage *msg)
    {
        srs_freep(videoSh);
        videoSh = msg->copy();
    }
    
    void set_audio_sh(SrsSharedPtrMessage *msg)
    {
        srs_freep(audioSh);
        audioSh = msg->copy();
    }
    
    string fragment_path()
    {
        return path;
    }
    
private:
    SrsRequest *req;
    list<SrsSharedPtrMessage *> msgs;
    
    /*!
     the index of this fragment
     */
    int index;
    long long start_time;
    
    SrsSharedPtrMessage *videoSh;
    SrsSharedPtrMessage *audioSh;
    string path;
};

SrsHds::SrsHds()
: currentSegment(NULL)
, fragment_index(1)
, video_sh(NULL)
, audio_sh(NULL)
, hds_req(NULL)
, hds_enabled(false)
{
    
}

SrsHds::~SrsHds()
{
}

srs_error_t SrsHds::on_publish(SrsRequest *req)
{
    srs_error_t err = srs_success;
    
    if (hds_enabled) {
        return err;
    }
    
    std::string vhost = req->vhost;
    if (!_srs_config->get_hds_enabled(vhost)) {
        hds_enabled = false;
        return err;
    }
    hds_enabled = true;
    
    hds_req = req->copy();
    
    return flush_mainfest();
}

srs_error_t SrsHds::on_unpublish()
{
    srs_error_t err = srs_success;
    
    if (!hds_enabled) {
        return err;
    }
    
    hds_enabled = false;
    
    srs_freep(video_sh);
    srs_freep(audio_sh);
    srs_freep(hds_req);
    
    // clean fragments
    list<SrsHdsFragment *>::iterator iter;
    for (iter = fragments.begin(); iter != fragments.end(); ++iter) {
        SrsHdsFragment *st = *iter;
        srs_freep(st);
    }
    fragments.clear();
    
    srs_freep(currentSegment);
    
    srs_trace("HDS un-published");
    
    return err;
}

srs_error_t SrsHds::on_video(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;
    
    if (!hds_enabled) {
        return err;
    }
    
    if (SrsFlvVideo::sh(msg->payload, msg->size)) {
        srs_freep(video_sh);
        video_sh = msg->copy();
    }
    
    if (!currentSegment) {
        currentSegment = new SrsHdsFragment(hds_req);
        currentSegment->set_index(fragment_index++);
        currentSegment->set_start_time(msg->timestamp);
        
        if (video_sh)
            currentSegment->set_video_sh(video_sh);
        
        if (audio_sh)
            currentSegment->set_audio_sh(audio_sh);
    }
    
    currentSegment->on_video(msg);
    
    double fragment_duration = srsu2ms(_srs_config->get_hds_fragment(hds_req->vhost));
    if (currentSegment->duration() >= fragment_duration) {
        // flush segment
        if ((err = currentSegment->flush()) != srs_success) {
            return srs_error_wrap(err, "flush segment");
        }
        
        srs_trace("flush Segment success.");
        fragments.push_back(currentSegment);
        currentSegment = NULL;
        adjust_windows();
        
        // flush bootstrap
        if ((err = flush_bootstrap()) != srs_success) {
            return srs_error_wrap(err, "flush bootstrap");
        }
        
        srs_trace("flush BootStrap success.");
    }
    
    return err;
}

srs_error_t SrsHds::on_audio(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;
    
    if (!hds_enabled) {
        return err;
    }
    
    if (SrsFlvAudio::sh(msg->payload, msg->size)) {
        srs_freep(audio_sh);
        audio_sh = msg->copy();
    }
    
    if (!currentSegment) {
        currentSegment = new SrsHdsFragment(hds_req);
        currentSegment->set_index(fragment_index++);
        currentSegment->set_start_time(msg->timestamp);
        
        if (video_sh)
            currentSegment->set_video_sh(video_sh);
        
        if (audio_sh)
            currentSegment->set_audio_sh(audio_sh);
    }
    
    currentSegment->on_audio(msg);
    
    double fragment_duration = srsu2ms(_srs_config->get_hds_fragment(hds_req->vhost));
    if (currentSegment->duration() >= fragment_duration) {
        // flush segment
        if ((err = currentSegment->flush()) != srs_success) {
            return srs_error_wrap(err, "flush segment");
        }
        
        srs_info("flush Segment success.");
        
        // reset the current segment
        fragments.push_back(currentSegment);
        currentSegment = NULL;
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
            "</manifest>"
            , hds_req->stream.c_str(), hds_req->stream.c_str(), hds_req->stream.c_str());
    
    string dir = _srs_config->get_hds_path(hds_req->vhost) + "/" + hds_req->app;
    if ((err = srs_create_dir_recursively(dir)) != srs_success) {
        return srs_error_wrap(err, "hds create dir failed");
    }
    string path = dir + "/" + hds_req->stream + ".f4m";
    
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
    
    int size = 1024*100;

    SrsUniquePtr<char[]> start_abst(new char[1024*100]);

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
    abst.write_1bytes(0x00);           // Either 0 or 1
    abst.write_3bytes(0x00);           // Flags always 0
    size_abst += 12;
    /*!
     @BootstrapinfoVersion       UI32
     The version number of the bootstrap information.
     When the Update field is set, BootstrapinfoVersion
     indicates the version number that is being updated.
     we assume this is the last.
     */
    abst.write_4bytes(fragment_index - 1);            // BootstrapinfoVersion
    
    abst.write_1bytes(0x20);                       // profile, live, update
    abst.write_4bytes(1000);                       // TimeScale Typically, the value is 1000, for a unit of milliseconds
    size_abst += 9;
    /*!
     The timestamp in TimeScale units of the latest available Fragment in the media presentation.
     This timestamp is used to request the right fragment number.
     The CurrentMedia Time can be the total duration.
     For media presentations that are not live, CurrentMediaTime can be 0.
     */
    SrsHdsFragment *st = fragments.back();
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
    for  (int i = 0; i < 1; ++i) {
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
        abst.write_4bytes(fragment_index - 1);
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
    abst.write_4bytes((int32_t)fragments.size());
    size_afrt += 4;
    
    list<SrsHdsFragment *>::iterator iter;
    for (iter = fragments.begin(); iter != fragments.end(); ++iter) {
        SrsHdsFragment *st = *iter;
        abst.write_4bytes(st->get_index());
        abst.write_8bytes(st->get_start_time());
        abst.write_4bytes(st->duration());
        size_afrt += 16;
    }
    
    update_box(start_afrt, size_afrt);
    size_abst += size_afrt;
    update_box(start_abst.get(), size_abst);
    
    string path = _srs_config->get_hds_path(hds_req->vhost) + "/" + hds_req->app + "/" + hds_req->stream +".abst";
    
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
    for (iter = fragments.begin(); iter != fragments.end(); ++iter) {
        SrsHdsFragment *fragment = *iter;
        windows_size += fragment->duration();
    }
    
    double windows_size_limit = srsu2ms(_srs_config->get_hds_window(hds_req->vhost));
    if (windows_size > windows_size_limit ) {
        SrsHdsFragment *fragment = fragments.front();
        unlink(fragment->fragment_path().c_str());
        fragments.erase(fragments.begin());
        srs_freep(fragment);
    }
}

#endif
