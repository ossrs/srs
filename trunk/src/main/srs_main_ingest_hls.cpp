//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_core.hpp>

#include <stdlib.h>
#include <string>
#include <vector>
#include <map>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_log.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_threads.hpp>

// pre-declare
srs_error_t proxy_hls2rtmp(std::string hls, std::string rtmp);

// @global log and context.
ISrsLog* _srs_log = new SrsConsoleLog(SrsLogLevelTrace, false);
ISrsContext* _srs_context = new SrsThreadContext();

// @global config object for app module.
SrsConfig* _srs_config = new SrsConfig();

// @global Other variables.
bool _srs_in_docker = false;

// Whether setup config by environment variables, see https://github.com/ossrs/srs/issues/2277
bool _srs_config_by_env = false;

// The binary name of SRS.
const char* _srs_binary = NULL;

/**
 * main entrance.
 */
int main(int argc, char** argv)
{
    // TODO: support both little and big endian.
    srs_assert(srs_is_little_endian());

    _srs_binary = argv[0];
    
    // directly failed when compile limited.
#if defined(SRS_GPERF_MP) || defined(SRS_GPERF_MP) \
    || defined(SRS_GPERF_MC) || defined(SRS_GPERF_MP)
    srs_error("donot support gmc/gmp/gcp/gprof");
    exit(-1);
#endif

    srs_error_t err = srs_success;
    if ((err = srs_global_initialize()) != srs_success) {
        srs_freep(err);
        srs_error("global init error");
        return -1;
    }

    srs_trace("srs_ingest_hls base on %s, to ingest hls live to srs", RTMP_SIG_SRS_SERVER);
    
    // parse user options.
    std::string in_hls_url, out_rtmp_url;
    for (int opt = 0; opt < argc; opt++) {
        srs_trace("argv[%d]=%s", opt, argv[opt]);
    }
    
    // fill the options for mac
    for (int opt = 0; opt < argc - 1; opt++) {
        // ignore all options except -i and -y.
        char* p = argv[opt];
        
        // only accept -x
        if (p[0] != '-' || p[1] == 0 || p[2] != 0) {
            continue;
        }
        
        // parse according the option name.
        switch (p[1]) {
            case 'i': in_hls_url = argv[opt + 1]; break;
            case 'y': out_rtmp_url = argv[opt + 1]; break;
            default: break;
        }
    }
    
    if (in_hls_url.empty() || out_rtmp_url.empty()) {
        printf("ingest hls live stream and publish to RTMP server\n"
               "Usage: %s <-i in_hls_url> <-y out_rtmp_url>\n"
               "   in_hls_url      input hls url, ingest from this m3u8.\n"
               "   out_rtmp_url    output rtmp url, publish to this url.\n"
               "For example:\n"
               "   %s -i http://127.0.0.1:8080/live/livestream.m3u8 -y rtmp://127.0.0.1/live/ingest_hls\n"
               "   %s -i http://ossrs.net/live/livestream.m3u8 -y rtmp://127.0.0.1/live/ingest_hls\n",
               argv[0], argv[0], argv[0]);
        exit(-1);
    }
    
    srs_trace("input:  %s", in_hls_url.c_str());
    srs_trace("output: %s", out_rtmp_url.c_str());
    
    err = proxy_hls2rtmp(in_hls_url, out_rtmp_url);
    
    int ret = srs_error_code(err);
    srs_freep(err);
    return ret;
}

class ISrsAacHandler
{
public:
    /**
     * handle the aac frame, which in ADTS format(starts with FFFx).
     * @param duration the duration in seconds of frames.
     */
    virtual int on_aac_frame(char* frame, int frame_size, double duration) = 0;
};

// the context to ingest hls stream.
class SrsIngestHlsInput
{
private:
    struct SrsTsPiece {
        double duration;
        std::string url;
        std::string body;
        
        // should skip this ts?
        bool skip;
        // already sent to rtmp server?
        bool sent;
        // whether ts piece is dirty, remove if not update.
        bool dirty;
        
        SrsTsPiece() {
            skip = false;
            sent = false;
            dirty = false;
            duration = 0.0;
        }
        
        int fetch(std::string m3u8);
    };
private:
    SrsHttpUri* in_hls;
    std::vector<SrsTsPiece*> pieces;
    srs_utime_t next_connect_time;
private:
    SrsTsContext* context;
public:
    SrsIngestHlsInput(SrsHttpUri* hls) {
        in_hls = hls;
        next_connect_time = 0;
        context = new SrsTsContext();
    }
    virtual ~SrsIngestHlsInput() {
        srs_freep(context);
        
        std::vector<SrsTsPiece*>::iterator it;
        for (it = pieces.begin(); it != pieces.end(); ++it) {
            SrsTsPiece* tp = *it;
            srs_freep(tp);
        }
        pieces.clear();
    }
    /**
     * parse the input hls live m3u8 index.
     */
    virtual int connect();
    /**
     * parse the ts and use hanler to process the message.
     */
    virtual int parse(ISrsTsHandler* ts, ISrsAacHandler* aac);
private:
    /**
     * parse the ts pieces body.
     */
    virtual int parseAac(ISrsAacHandler* handler, char* body, int nb_body, double duration);
    virtual int parseTs(ISrsTsHandler* handler, char* body, int nb_body);
    /**
     * parse the m3u8 specified by url.
     */
    virtual int parseM3u8(SrsHttpUri* url, double& td, double& duration);
    /**
     * find the ts piece by its url.
     */
    virtual SrsTsPiece* find_ts(string url);
    /**
     * set all ts to dirty.
     */
    virtual void dirty_all_ts();
    /**
     * fetch all ts body.
     */
    virtual int fetch_all_ts(bool fresh_m3u8);
    /**
     * remove all ts which is dirty.
     */
    virtual void remove_dirty();
};

int SrsIngestHlsInput::connect()
{
    int ret = ERROR_SUCCESS;
    
    srs_utime_t now = srs_update_system_time();
    if (now < next_connect_time) {
        srs_trace("input hls wait for %dms", srsu2msi(next_connect_time - now));
        srs_usleep(next_connect_time - now);
    }
    
    // set all ts to dirty.
    dirty_all_ts();
    
    bool fresh_m3u8 = pieces.empty();
    double td = 0.0;
    double duration = 0.0;
    if ((ret = parseM3u8(in_hls, td, duration)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // fetch all ts.
    if ((ret = fetch_all_ts(fresh_m3u8)) != ERROR_SUCCESS) {
        srs_error("fetch all ts failed. ret=%d", ret);
        return ret;
    }
    
    // remove all dirty ts.
    remove_dirty();
    
    srs_trace("fetch m3u8 ok, td=%.2f, duration=%.2f, pieces=%d", td, duration, pieces.size());
    
    return ret;
}

int SrsIngestHlsInput::parse(ISrsTsHandler* ts, ISrsAacHandler* aac)
{
    int ret = ERROR_SUCCESS;
    
    for (int i = 0; i < (int)pieces.size(); i++) {
        SrsTsPiece* tp = pieces.at(i);
        
        // sent only once.
        if (tp->sent) {
            continue;
        }
        tp->sent = true;
        
        if (tp->body.empty()) {
            continue;
        }
        
        srs_trace("proxy the ts to rtmp, ts=%s, duration=%.2f", tp->url.c_str(), tp->duration);
        
        if (srs_string_ends_with(tp->url, ".ts")) {
            if ((ret = parseTs(ts, (char*)tp->body.data(), (int)tp->body.length())) != ERROR_SUCCESS) {
                return ret;
            }
        } else if (srs_string_ends_with(tp->url, ".aac")) {
            if ((ret = parseAac(aac, (char*)tp->body.data(), (int)tp->body.length(), tp->duration)) != ERROR_SUCCESS) {
                return ret;
            }
        } else {
            srs_warn("ignore unkown piece %s", tp->url.c_str());
        }
    }
    
    return ret;
}

int SrsIngestHlsInput::parseTs(ISrsTsHandler* handler, char* body, int nb_body)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    // use stream to parse ts packet.
    int nb_packet = (int)nb_body / SRS_TS_PACKET_SIZE;
    for (int i = 0; i < nb_packet; i++) {
        char* p = (char*)body + (i * SRS_TS_PACKET_SIZE);
        SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(p, SRS_TS_PACKET_SIZE));

        // process each ts packet
        if ((err = context->decode(stream.get(), handler)) != srs_success) {
            // TODO: FIXME: Use error
            ret = srs_error_code(err);
            srs_freep(err);
            srs_error("mpegts: ignore parse ts packet failed. ret=%d", ret);
            return ret;
        }
        srs_info("mpegts: parse ts packet completed");
    }
    srs_info("mpegts: parse udp packet completed");
    
    return ret;
}

int SrsIngestHlsInput::parseAac(ISrsAacHandler* handler, char* body, int nb_body, double duration)
{
    int ret = ERROR_SUCCESS;

    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(body, nb_body));

    // atleast 2bytes.
    if (!stream->require(3)) {
        ret = ERROR_AAC_BYTES_INVALID;
        srs_error("invalid aac, atleast 3bytes. ret=%d", ret);
        return ret;
    }
    
    uint8_t id0 = (uint8_t)body[0];
    uint8_t id1 = (uint8_t)body[1];
    uint8_t id2 = (uint8_t)body[2];
    
    // skip ID3.
    if (id0 == 0x49 && id1 == 0x44 && id2 == 0x33) {
        /*char id3[] = {
         (char)0x49, (char)0x44, (char)0x33, // ID3
         (char)0x03, (char)0x00, // version
         (char)0x00, // flags
         (char)0x00, (char)0x00, (char)0x00, (char)0x0a, // size
         
         (char)0x00, (char)0x00, (char)0x00, (char)0x00, // FrameID
         (char)0x00, (char)0x00, (char)0x00, (char)0x00, // FrameSize
         (char)0x00, (char)0x00 // Flags
         };*/
        // atleast 10 bytes.
        if (!stream->require(10)) {
            ret = ERROR_AAC_BYTES_INVALID;
            srs_error("invalid aac ID3, atleast 10bytes. ret=%d", ret);
            return ret;
        }
        
        // ignore ID3 + version + flag.
        stream->skip(6);
        // read the size of ID3.
        uint32_t nb_id3 = stream->read_4bytes();
        
        // read body of ID3
        if (!stream->require(nb_id3)) {
            ret = ERROR_AAC_BYTES_INVALID;
            srs_error("invalid aac ID3 body, required %dbytes. ret=%d", nb_id3, ret);
            return ret;
        }
        stream->skip(nb_id3);
    }
    
    char* frame = body + stream->pos();
    int frame_size = nb_body - stream->pos();
    return handler->on_aac_frame(frame, frame_size, duration);
}

int SrsIngestHlsInput::parseM3u8(SrsHttpUri* url, double& td, double& duration)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    SrsHttpClient client;
    srs_trace("parse input hls %s", url->get_url().c_str());
    
    if ((err = client.initialize(url->get_schema(), url->get_host(), url->get_port())) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        srs_error("connect to server failed. ret=%d", ret);
        return ret;
    }
    
    ISrsHttpMessage* msg_raw = NULL;
    if ((err = client.get(url->get_path(), "", &msg_raw)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        srs_error("HTTP GET %s failed. ret=%d", url->get_url().c_str(), ret);
        return ret;
    }
    
    srs_assert(msg_raw);
    SrsUniquePtr<ISrsHttpMessage> msg(msg_raw);
    
    std::string body;
    if ((err = msg->body_read_all(body)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        srs_error("read m3u8 failed. ret=%d", ret);
        return ret;
    }
    
    if (body.empty()) {
        srs_warn("ignore empty m3u8");
        return ret;
    }
    
    std::string ptl;
    while (!body.empty()) {
        size_t pos = string::npos;
        
        std::string line;
        if ((pos = body.find("\n")) != string::npos) {
            line = body.substr(0, pos);
            body = body.substr(pos + 1);
        } else {
            line = body;
            body = "";
        }
        
        line = srs_string_replace(line, "\r", "");
        line = srs_string_replace(line, " ", "");
        
        // #EXT-X-VERSION:3
        // the version must be 3.0
        if (srs_string_starts_with(line, "#EXT-X-VERSION:")) {
            if (!srs_string_ends_with(line, ":3")) {
                srs_warn("m3u8 3.0 required, actual is %s", line.c_str());
            }
            continue;
        }
        
        // #EXT-X-PLAYLIST-TYPE:VOD
        // the playlist type, vod or nothing.
        if (srs_string_starts_with(line, "#EXT-X-PLAYLIST-TYPE:")) {
            ptl = line;
            continue;
        }
        
        // #EXT-X-TARGETDURATION:12
        // the target duration is required.
        if (srs_string_starts_with(line, "#EXT-X-TARGETDURATION:")) {
            td = ::atof(line.substr(string("#EXT-X-TARGETDURATION:").length()).c_str());
        }
        
        // #EXT-X-ENDLIST
        // parse completed.
        if (line == "#EXT-X-ENDLIST") {
            break;
        }
        
        // #EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=73207,CODECS="mp4a.40.2"
        if (srs_string_starts_with(line, "#EXT-X-STREAM-INF:")) {
            if ((pos = body.find("\n")) == string::npos) {
                srs_warn("m3u8 entry unexpected eof, inf=%s", line.c_str());
                break;
            }
            
            std::string m3u8_url = body.substr(0, pos);
            body = body.substr(pos + 1);
            
            if (!srs_string_is_http(m3u8_url)) {
                m3u8_url = srs_path_dirname(url->get_url()) + "/" + m3u8_url;
            }
            srs_trace("parse sub m3u8, url=%s", m3u8_url.c_str());
            
            if ((err = url->initialize(m3u8_url)) != srs_success) {
                // TODO: FIXME: Use error
                ret = srs_error_code(err);
                srs_freep(err);
                return ret;
            }
            
            return parseM3u8(url, td, duration);
        }
        
        // #EXTINF:11.401,
        // livestream-5.ts
        // parse each ts entry, expect current line is inf.
        if (!srs_string_starts_with(line, "#EXTINF:")) {
            continue;
        }
        
        // expect next line is url.
        std::string ts_url;
        if ((pos = body.find("\n")) != string::npos) {
            ts_url = body.substr(0, pos);
            body = body.substr(pos + 1);
        } else {
            srs_warn("ts entry unexpected eof, inf=%s", line.c_str());
            break;
        }
        
        // parse the ts duration.
        line = line.substr(string("#EXTINF:").length());
        if ((pos = line.find(",")) != string::npos) {
            line = line.substr(0, pos);
        }
        
        double ts_duration = ::atof(line.c_str());
        duration += ts_duration;
        
        SrsTsPiece* tp = find_ts(ts_url);
        if (!tp) {
            tp = new SrsTsPiece();
            tp->url = ts_url;
            tp->duration = ts_duration;
            pieces.push_back(tp);
        } else {
            tp->dirty = false;
        }
    }
    
    return ret;
}

SrsIngestHlsInput::SrsTsPiece* SrsIngestHlsInput::find_ts(string url)
{
    std::vector<SrsTsPiece*>::iterator it;
    for (it = pieces.begin(); it != pieces.end(); ++it) {
        SrsTsPiece* tp = *it;
        if (tp->url == url) {
            return tp;
        }
    }
    return NULL;
}

void SrsIngestHlsInput::dirty_all_ts()
{
    std::vector<SrsTsPiece*>::iterator it;
    for (it = pieces.begin(); it != pieces.end(); ++it) {
        SrsTsPiece* tp = *it;
        tp->dirty = true;
    }
}

int SrsIngestHlsInput::fetch_all_ts(bool fresh_m3u8)
{
    int ret = ERROR_SUCCESS;
    
    for (int i = 0; i < (int)pieces.size(); i++) {
        SrsTsPiece* tp = pieces.at(i);
        
        // when skipped, ignore.
        if (tp->skip) {
            continue;
        }
        
        // for the fresh m3u8, skip except the last one.
        if (fresh_m3u8 && i != (int)pieces.size() - 1) {
            tp->skip = true;
            continue;
        }
        
        if ((ret = tp->fetch(in_hls->get_url())) != ERROR_SUCCESS) {
            srs_error("fetch ts %s for error. ret=%d", tp->url.c_str(), ret);
            tp->skip = true;
            return ret;
        }
        
        // only wait for a duration of last piece.
        if (i == (int)pieces.size() - 1) {
            next_connect_time = srs_update_system_time() + tp->duration * SRS_UTIME_SECONDS;
        }
    }
    
    return ret;
}


void SrsIngestHlsInput::remove_dirty()
{
    std::vector<SrsTsPiece*>::iterator it;
    for (it = pieces.begin(); it != pieces.end();) {
        SrsTsPiece* tp = *it;
        
        if (tp->dirty) {
            srs_trace("erase dirty ts, url=%s, duration=%.2f", tp->url.c_str(), tp->duration);
            srs_freep(tp);
            it = pieces.erase(it);
        } else {
            ++it;
        }
    }
}

int SrsIngestHlsInput::SrsTsPiece::fetch(string m3u8)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if (skip || sent || !body.empty()) {
        return ret;
    }
    
    SrsHttpClient client;
    
    std::string ts_url = url;
    if (!srs_string_is_http(ts_url)) {
        ts_url = srs_path_dirname(m3u8) + "/" + url;
    }
    
    SrsHttpUri uri;
    if ((err = uri.initialize(ts_url)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        return ret;
    }
    
    // initialize the fresh http client.
    if ((ret = client.initialize(uri.get_schema(), uri.get_host(), uri.get_port()) != srs_success)) {
        return ret;
    }
    
    ISrsHttpMessage* msg_raw = NULL;
    if ((err = client.get(uri.get_path(), "", &msg_raw)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        srs_error("HTTP GET %s failed. ret=%d", uri.get_url().c_str(), ret);
        return ret;
    }
    
    srs_assert(msg_raw);
    SrsUniquePtr<ISrsHttpMessage> msg(msg_raw);
    
    if ((err = msg->body_read_all(body)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        srs_error("read ts failed. ret=%d", ret);
        return ret;
    }
    
    srs_trace("fetch ts ok, duration=%.2f, url=%s, body=%dB", duration, url.c_str(), body.length());
    
    return ret;
}

// the context to output to rtmp server
class SrsIngestHlsOutput : public ISrsTsHandler, public ISrsAacHandler
{
private:
    SrsHttpUri* out_rtmp;
private:
    bool disconnected;
    std::multimap<int64_t, SrsTsMessage*> queue;
    int64_t raw_aac_dts;
private:
    SrsRequest* req;
    SrsBasicRtmpClient* sdk;
private:
    SrsRawH264Stream* avc;
    std::string h264_sps;
    bool h264_sps_changed;
    std::string h264_pps;
    bool h264_pps_changed;
    bool h264_sps_pps_sent;
private:
    SrsRawAacStream* aac;
    std::string aac_specific_config;
public:
    SrsIngestHlsOutput(SrsHttpUri* rtmp);
    virtual ~SrsIngestHlsOutput();
// Interface ISrsTsHandler
public:
    virtual srs_error_t on_ts_message(SrsTsMessage* msg);
// Interface IAacHandler
public:
    virtual int on_aac_frame(char* frame, int frame_size, double duration);
private:
    virtual int do_on_aac_frame(SrsBuffer* avs, double duration);
    virtual int parse_message_queue();
    virtual int on_ts_video(SrsTsMessage* msg, SrsBuffer* avs);
    virtual int write_h264_sps_pps(uint32_t dts, uint32_t pts);
    virtual int write_h264_ipb_frame(std::string ibps, SrsVideoAvcFrameType frame_type, uint32_t dts, uint32_t pts);
    virtual int on_ts_audio(SrsTsMessage* msg, SrsBuffer* avs);
    virtual int write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts);
private:
    virtual int rtmp_write_packet(char type, uint32_t timestamp, char* data, int size);
public:
    /**
     * connect to output rtmp server.
     */
    virtual int connect();
    /**
     * flush the message queue when all ts parsed.
     */
    virtual int flush_message_queue();
private:
    // close the connected io and rtmp to ready to be re-connect.
    virtual void close();
};

SrsIngestHlsOutput::SrsIngestHlsOutput(SrsHttpUri* rtmp)
{
    out_rtmp = rtmp;
    disconnected = false;
    raw_aac_dts = srsu2ms(srs_update_system_time());
    
    req = NULL;
    sdk = NULL;
    
    avc = new SrsRawH264Stream();
    aac = new SrsRawAacStream();
    h264_sps_changed = false;
    h264_pps_changed = false;
    h264_sps_pps_sent = false;
}

SrsIngestHlsOutput::~SrsIngestHlsOutput()
{
    close();
    
    srs_freep(avc);
    srs_freep(aac);
    
    std::multimap<int64_t, SrsTsMessage*>::iterator it;
    for (it = queue.begin(); it != queue.end(); ++it) {
        SrsTsMessage* msg = it->second;
        srs_freep(msg);
    }
    queue.clear();
}

srs_error_t SrsIngestHlsOutput::on_ts_message(SrsTsMessage* msg)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    // about the bytes of msg, specified by elementary stream which indicates by PES_packet_data_byte and stream_id
    // for example, when SrsTsStream of SrsTsChannel indicates stream_type is SrsTsStreamVideoMpeg4 and SrsTsStreamAudioMpeg4,
    // the elementary stream can be mux in "2.11 Carriage of ISO/IEC 14496 data" in hls-mpeg-ts-iso13818-1.pdf, page 103
    // @remark, the most popular stream_id is 0xe0 for h.264 over mpegts, which indicates the stream_id is video and
    //      stream_number is 0, where I guess the elementary is specified in annexb format(ISO_IEC_14496-10-AVC-2003.pdf, page 211).
    //      because when audio stream_number is 0, the elementary is ADTS(ISO_IEC_14496-3-AAC-2001.pdf, page 75, 1.A.2.2 ADTS).
    
    // about the bytes of PES_packet_data_byte, defined in hls-mpeg-ts-iso13818-1.pdf, page 58
    // PES_packet_data_byte "C PES_packet_data_bytes shall be contiguous bytes of data from the elementary stream
    // indicated by the packet¡¯s stream_id or PID. When the elementary stream data conforms to ITU-T
    // Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 13818-3, the PES_packet_data_bytes shall be byte aligned to the bytes of this
    // Recommendation | International Standard. The byte-order of the elementary stream shall be preserved. The number of
    // PES_packet_data_bytes, N, is specified by the PES_packet_length field. N shall be equal to the value indicated in the
    // PES_packet_length minus the number of bytes between the last byte of the PES_packet_length field and the first
    // PES_packet_data_byte.
    //
    // In the case of a private_stream_1, private_stream_2, ECM_stream, or EMM_stream, the contents of the
    // PES_packet_data_byte field are user definable and will not be specified by ITU-T | ISO/IEC in the future.
    
    // about the bytes of stream_id, define in  hls-mpeg-ts-iso13818-1.pdf, page 49
    // stream_id "C In Program Streams, the stream_id specifies the type and number of the elementary stream as defined by the
    // stream_id Table 2-18. In Transport Streams, the stream_id may be set to any valid value which correctly describes the
    // elementary stream type as defined in Table 2-18. In Transport Streams, the elementary stream type is specified in the
    // Program Specific Information as specified in 2.4.4.
    
    // about the stream_id table, define in Table 2-18 "C Stream_id assignments, hls-mpeg-ts-iso13818-1.pdf, page 52.
    //
    // 110x xxxx
    // ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7 or ISO/IEC
    // 14496-3 audio stream number x xxxx
    // ((sid >> 5) & 0x07) == SrsTsPESStreamIdAudio
    //
    // 1110 xxxx
    // ITU-T Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 11172-2 or ISO/IEC
    // 14496-2 video stream number xxxx
    // ((stream_id >> 4) & 0x0f) == SrsTsPESStreamIdVideo
    
    srs_info("<- " SRS_CONSTS_LOG_STREAM_CASTER " mpegts: got %s stream=%s, dts=%" PRId64 ", pts=%" PRId64 ", size=%d, us=%d, cc=%d, sid=%#x(%s-%d)",
             (msg->channel->apply == SrsTsPidApplyVideo)? "Video":"Audio", srs_ts_stream2string(msg->channel->stream).c_str(),
             msg->dts, msg->pts, msg->payload->length(), msg->packet->payload_unit_start_indicator, msg->continuity_counter, msg->sid,
             msg->is_audio()? "A":msg->is_video()? "V":"N", msg->stream_number());
    
    // When the audio SID is private stream 1, we use common audio.
    // @see https://github.com/ossrs/srs/issues/740
    if (msg->channel->apply == SrsTsPidApplyAudio && msg->sid == SrsTsPESStreamIdPrivateStream1) {
        msg->sid = SrsTsPESStreamIdAudioCommon;
    }
    
    // when not audio/video, or not adts/annexb format, donot support.
    if (msg->stream_number() != 0) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_ES, "ts: unsupported stream format, sid=%#x(%s-%d)",
            msg->sid, msg->is_audio()? "A":msg->is_video()? "V":"N", msg->stream_number());
    }
    
    // check supported codec
    if (msg->channel->stream != SrsTsStreamVideoH264 && msg->channel->stream != SrsTsStreamAudioAAC) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_CODEC, "ts: unsupported stream codec=%d", msg->channel->stream);
    }
    
    // we must use queue to cache the msg, then parse it if possible.
    queue.insert(std::make_pair(msg->dts, msg->detach()));
    if ((ret = parse_message_queue()) != ERROR_SUCCESS) {
        return srs_error_new(ret, "ts: parse message");
    }
    
    return err;
}

int SrsIngestHlsOutput::on_aac_frame(char* frame, int frame_size, double duration)
{
    srs_trace("handle aac frames, size=%dB, duration=%.2f, dts=%" PRId64, frame_size, duration, raw_aac_dts);
    
    SrsBuffer stream(frame, frame_size);
    return do_on_aac_frame(&stream, duration);
}

int SrsIngestHlsOutput::do_on_aac_frame(SrsBuffer* avs, double duration)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    uint32_t duration_ms = (uint32_t)(duration * 1000);
    
    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)raw_aac_dts;
    raw_aac_dts += duration_ms;
    
    // got the next msg to calc the delta duration for each audio.
    uint32_t max_dts = dts + duration_ms;
    
    // send each frame.
    while (!avs->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((err = aac->adts_demux(avs, &frame, &frame_size, codec)) != srs_success) {
            // TODO: FIXME: Use error
            ret = srs_error_code(err);
            srs_freep(err);
            return ret;
        }
        
        // ignore invalid frame,
        //  * atleast 1bytes for aac to decode the data.
        if (frame_size <= 0) {
            continue;
        }
        srs_info("mpegts: demux aac frame size=%d, dts=%d", frame_size, dts);
        
        // generate sh.
        if (aac_specific_config.empty()) {
            std::string sh;
            if ((err = aac->mux_sequence_header(&codec, sh)) != srs_success) {
                // TODO: FIXME: Use error
                ret = srs_error_code(err);
                srs_freep(err);
                return ret;
            }
            aac_specific_config = sh;
            
            codec.aac_packet_type = 0;
            
            if ((ret = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), &codec, dts)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        
        // audio raw data.
        codec.aac_packet_type = 1;
        if ((ret = write_audio_raw_frame(frame, frame_size, &codec, dts)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // calc the delta of dts, when previous frame output.
        uint32_t delta = duration_ms / (avs->size() / frame_size);
        dts = (uint32_t)(srs_min(max_dts, dts + delta));
    }
    
    return ret;
}

int SrsIngestHlsOutput::parse_message_queue()
{
    int ret = ERROR_SUCCESS;
    
    if (queue.empty()) {
        return ret;
    }
    
    SrsTsMessage* first_ts_msg = queue.begin()->second;
    SrsTsContext* context = first_ts_msg->channel->context;
    bool cpa = context->is_pure_audio();
    
    int nb_videos = 0;
    if (!cpa) {
        std::multimap<int64_t, SrsTsMessage*>::iterator it;
        for (it = queue.begin(); it != queue.end(); ++it) {
            SrsTsMessage* msg = it->second;
            
            // publish audio or video.
            if (msg->channel->stream == SrsTsStreamVideoH264) {
                nb_videos++;
            }
        }
        
        // always wait 2+ videos, to left one video in the queue.
        // TODO: FIXME: support pure audio hls.
        if (nb_videos <= 1) {
            return ret;
        }
    }
    
    // parse messages util the last video.
    while ((cpa && queue.size() > 1) || nb_videos > 1) {
        srs_assert(!queue.empty());
        std::multimap<int64_t, SrsTsMessage*>::iterator it = queue.begin();

        SrsUniquePtr<SrsTsMessage> msg(it->second);
        queue.erase(it);
        
        if (msg->channel->stream == SrsTsStreamVideoH264) {
            nb_videos--;
        }
        
        // parse the stream.
        SrsBuffer avs(msg->payload->bytes(), msg->payload->length());
        
        // publish audio or video.
        if (msg->channel->stream == SrsTsStreamVideoH264) {
            if ((ret = on_ts_video(msg.get(), &avs)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        if (msg->channel->stream == SrsTsStreamAudioAAC) {
            if ((ret = on_ts_audio(msg.get(), &avs)) != ERROR_SUCCESS) {
                return ret;
            }
        }
    }
    
    return ret;
}

int SrsIngestHlsOutput::flush_message_queue()
{
    int ret = ERROR_SUCCESS;
    
    // parse messages util the last video.
    while (!queue.empty()) {
        std::multimap<int64_t, SrsTsMessage*>::iterator it = queue.begin();

        SrsUniquePtr<SrsTsMessage> msg(it->second);
        queue.erase(it);
        
        // parse the stream.
        SrsBuffer avs(msg->payload->bytes(), msg->payload->length());
        
        // publish audio or video.
        if (msg->channel->stream == SrsTsStreamVideoH264) {
            if ((ret = on_ts_video(msg.get(), &avs)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        if (msg->channel->stream == SrsTsStreamAudioAAC) {
            if ((ret = on_ts_audio(msg.get(), &avs)) != ERROR_SUCCESS) {
                return ret;
            }
        }
    }
    
    return ret;
}

int SrsIngestHlsOutput::on_ts_video(SrsTsMessage* msg, SrsBuffer* avs)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts / 90);
    uint32_t pts = (uint32_t)(msg->dts / 90);
    
    std::string ibps;
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    
    // send each frame.
    while (!avs->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        if ((err = avc->annexb_demux(avs, &frame, &frame_size)) != srs_success) {
            // TODO: FIXME: Use error
            ret = srs_error_code(err);
            srs_freep(err);
            return ret;
        }
        
        // 5bits, 7.3.1 NAL unit syntax,
        // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
        //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
        SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
        
        // for IDR frame, the frame is keyframe.
        if (nal_unit_type == SrsAvcNaluTypeIDR) {
            frame_type = SrsVideoAvcFrameTypeKeyFrame;
        }
        
        // ignore the nalu type aud(9)
        if (nal_unit_type == SrsAvcNaluTypeAccessUnitDelimiter) {
            continue;
        }
        
        // for sps
        if (avc->is_sps(frame, frame_size)) {
            std::string sps;
            if ((err = avc->sps_demux(frame, frame_size, sps)) != srs_success) {
                // TODO: FIXME: Use error
                ret = srs_error_code(err);
                srs_freep(err);
                return ret;
            }
            
            if (h264_sps == sps) {
                continue;
            }
            h264_sps_changed = true;
            h264_sps = sps;
            continue;
        }
        
        // for pps
        if (avc->is_pps(frame, frame_size)) {
            std::string pps;
            if ((err = avc->pps_demux(frame, frame_size, pps)) != srs_success) {
                // TODO: FIXME: Use error
                ret = srs_error_code(err);
                srs_freep(err);
                return ret;
            }
            
            if (h264_pps == pps) {
                continue;
            }
            h264_pps_changed = true;
            h264_pps = pps;
            continue;
        }
        
        // ibp frame.
        std::string ibp;
        if ((err = avc->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
            // TODO: FIXME: Use error
            ret = srs_error_code(err);
            srs_freep(err);
            return ret;
        }
        ibps.append(ibp);
    }
    
    if ((ret = write_h264_sps_pps(dts, pts)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = write_h264_ipb_frame(ibps, frame_type, dts, pts)) != ERROR_SUCCESS) {
        // drop the ts message.
        if (ret == ERROR_H264_DROP_BEFORE_SPS_PPS) {
            return ERROR_SUCCESS;
        }
        return ret;
    }
    
    return ret;
}

int SrsIngestHlsOutput::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    // when sps or pps changed, update the sequence header,
    // for the pps maybe not changed while sps changed.
    // so, we must check when each video ts message frame parsed.
    if (h264_sps_pps_sent && !h264_sps_changed && !h264_pps_changed) {
        return ret;
    }
    
    // when not got sps/pps, wait.
    if (h264_pps.empty() || h264_sps.empty()) {
        return ret;
    }
    
    // h264 raw to h264 packet.
    std::string sh;
    if ((err = avc->mux_sequence_header(h264_sps, h264_pps, sh)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        return ret;
    }
    
    // h264 packet to flv packet.
    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        return ret;
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    if ((ret = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // reset sps and pps.
    h264_sps_changed = false;
    h264_pps_changed = false;
    h264_sps_pps_sent = true;
    srs_trace("hls: h264 sps/pps sent, sps=%dB, pps=%dB", h264_sps.length(), h264_pps.length());
    
    return ret;
}

int SrsIngestHlsOutput::write_h264_ipb_frame(string ibps, SrsVideoAvcFrameType frame_type, uint32_t dts, uint32_t pts)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    // when sps or pps not sent, ignore the packet.
    if (!h264_sps_pps_sent) {
        return ERROR_H264_DROP_BEFORE_SPS_PPS;
    }
    
    int8_t avc_packet_type = SrsVideoAvcFrameTraitNALU;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(ibps, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        return ret;
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    return rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv);
}

int SrsIngestHlsOutput::on_ts_audio(SrsTsMessage* msg, SrsBuffer* avs)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts / 90);
    
    // got the next msg to calc the delta duration for each audio.
    uint32_t duration = 0;
    if (!queue.empty()) {
        SrsTsMessage* nm = queue.begin()->second;
        duration = (uint32_t)(srs_max(0, nm->dts - msg->dts) / 90);
    }
    uint32_t max_dts = dts + duration;
    
    // send each frame.
    while (!avs->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((err = aac->adts_demux(avs, &frame, &frame_size, codec)) != srs_success) {
            // TODO: FIXME: Use error
            ret = srs_error_code(err);
            srs_freep(err);
            return ret;
        }
        
        // ignore invalid frame,
        //  * atleast 1bytes for aac to decode the data.
        if (frame_size <= 0) {
            continue;
        }
        srs_info("mpegts: demux aac frame size=%d, dts=%d", frame_size, dts);
        
        // generate sh.
        if (aac_specific_config.empty()) {
            std::string sh;
            if ((err = aac->mux_sequence_header(&codec, sh)) != srs_success) {
                // TODO: FIXME: Use error
                ret = srs_error_code(err);
                srs_freep(err);
                return ret;
            }
            aac_specific_config = sh;
            
            codec.aac_packet_type = 0;
            
            if ((ret = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), &codec, dts)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        
        // audio raw data.
        codec.aac_packet_type = 1;
        if ((ret = write_audio_raw_frame(frame, frame_size, &codec, dts)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // calc the delta of dts, when previous frame output.
        uint32_t delta = duration / (msg->payload->length() / frame_size);
        dts = (uint32_t)(srs_min(max_dts, dts + delta));
    }
    
    return ret;
}

int SrsIngestHlsOutput::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    char* data = NULL;
    int size = 0;
    if ((err = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        return ret;
    }
    
    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

int SrsIngestHlsOutput::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = connect()) != ERROR_SUCCESS) {
        return ret;
    }
    
    SrsSharedPtrMessage* msg = NULL;
    
    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, sdk->sid(), &msg)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        srs_error("mpegts: create shared ptr msg failed. ret=%d", ret);
        return ret;
    }
    srs_assert(msg);
    
    srs_info("RTMP type=%d, dts=%d, size=%d", type, timestamp, size);
    
    // send out encoded msg.
    if ((err = sdk->send_and_free_message(msg)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        close();
        srs_error("send RTMP type=%d, dts=%d, size=%d failed. ret=%d", type, timestamp, size, ret);
        return ret;
    }
    
    return ret;
}

int SrsIngestHlsOutput::connect()
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    // Ignore when connected.
    if (sdk) {
        return ret;
    }
    
    std::string url = out_rtmp->get_url();
    srs_trace("connect output=%s", url.c_str());
    
    // connect host.
    srs_utime_t cto =SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto =SRS_CONSTS_RTMP_PULSE;
    sdk = new SrsBasicRtmpClient(url, cto, sto);
    
    if ((err = sdk->connect()) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        close();
        srs_error("mpegts: connect %s failed, cto=%dms, sto=%dms. ret=%d", url.c_str(), srsu2msi(cto), srsu2msi(sto), ret);
        return ret;
    }
    
    // publish.
    if ((err = sdk->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);
        close();
        srs_error("mpegts: publish %s failed. ret=%d", url.c_str(), ret);
        return ret;
    }
    
    return ret;
}

void SrsIngestHlsOutput::close()
{
    h264_sps_pps_sent = false;
    srs_freep(req);
    srs_freep(sdk);
}

// the context for ingest hls stream.
class SrsIngestHlsContext
{
private:
    SrsIngestHlsInput* ic;
    SrsIngestHlsOutput* oc;
public:
    SrsIngestHlsContext(SrsHttpUri* hls, SrsHttpUri* rtmp) {
        ic = new SrsIngestHlsInput(hls);
        oc = new SrsIngestHlsOutput(rtmp);
    }
    virtual ~SrsIngestHlsContext() {
        srs_freep(ic);
        srs_freep(oc);
    }
    virtual int proxy() {
        int ret = ERROR_SUCCESS;
        
        if ((ret = ic->connect()) != ERROR_SUCCESS) {
            srs_error("connect oc failed. ret=%d", ret);
            return ret;
        }
        
        if ((ret = oc->connect()) != ERROR_SUCCESS) {
            srs_error("connect ic failed. ret=%d", ret);
            return ret;
        }
        
        if ((ret = ic->parse(oc, oc)) != ERROR_SUCCESS) {
            srs_error("proxy ts to rtmp failed. ret=%d", ret);
            return ret;
        }
        
        if ((ret = oc->flush_message_queue()) != ERROR_SUCCESS) {
            srs_error("flush oc message failed. ret=%d", ret);
            return ret;
        }
        
        return ret;
    }
};

srs_error_t proxy_hls2rtmp(string hls, string rtmp)
{
    srs_error_t err = srs_success;
    
    // init st.
    if ((err = srs_st_init()) != srs_success) {
        return srs_error_wrap(err, "initialize st");
    }
    
    SrsHttpUri hls_uri, rtmp_uri;
    if ((err = hls_uri.initialize(hls)) != srs_success) {
        return srs_error_wrap(err, "hls parse uri=%s", hls.c_str());
    }
    if ((err = rtmp_uri.initialize(rtmp)) != srs_success) {
        return srs_error_wrap(err, "rtmp parse uri=%s", rtmp.c_str());
    }
    
    SrsIngestHlsContext context(&hls_uri, &rtmp_uri);
    for (;;) {
        int ret = ERROR_SUCCESS;
        if ((ret = context.proxy()) != ERROR_SUCCESS) {
            return srs_error_new(ret, "proxy hls to rtmp");
        }
    }
    
    return err;
}

