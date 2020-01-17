#ifndef SRT_TO_RTMP_H
#define SRT_TO_RTMP_H
#include <memory>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <srs_kernel_ts.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_raw_avc.hpp>
#include <srs_protocol_utility.hpp>
#include <unordered_map>

#include "srt_data.hpp"

#define SRT_VIDEO_MSG_TYPE 0x01
#define SRT_AUDIO_MSG_TYPE 0x02

typedef std::shared_ptr<SrsSimpleRtmpClient> RTMP_CONN_PTR;
typedef std::shared_ptr<SrsRawH264Stream> AVC_PTR;
typedef std::shared_ptr<SrsRawAacStream> AAC_PTR;

class rtmp_client : public ISrsTsHandler {
public:
    rtmp_client(std::string key_path);
    ~rtmp_client();

    void receive_ts_data(SRT_DATA_MSG_PTR data_ptr);
    
private:
    virtual srs_error_t on_ts_message(SrsTsMessage* msg);
    srs_error_t connect();
    void close();

private:
    srs_error_t on_ts_video(SrsTsMessage* msg, SrsBuffer* avs);
    srs_error_t on_ts_audio(SrsTsMessage* msg, SrsBuffer* avs);
    virtual srs_error_t write_h264_sps_pps(uint32_t dts, uint32_t pts);
    virtual srs_error_t write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts);
    virtual srs_error_t write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts);

private:
    virtual srs_error_t rtmp_write_packet(char type, uint32_t timestamp, char* data, int size);

private:
    std::string _key_path;
    std::string _url;
    std::shared_ptr<SrsTsContext> _ts_ctx_ptr;

private:
    AVC_PTR _avc_ptr;
    std::string _h264_sps;
    bool _h264_sps_changed;
    std::string _h264_pps;
    bool _h264_pps_changed;
    bool _h264_sps_pps_sent;
private:
    std::string _aac_specific_config;
    AAC_PTR _aac_ptr;
private:
    RTMP_CONN_PTR _rtmp_conn_ptr;
};

typedef std::shared_ptr<rtmp_client> RTMP_CLIENT_PTR;

class srt2rtmp {
public:
    srt2rtmp();
    virtual ~srt2rtmp();

    void start();
    void stop();
    void insert_data_message(unsigned char* data_p, unsigned int len, const std::string& key_path);

private:
    SRT_DATA_MSG_PTR get_data_message();
    void on_work();
    void handle_ts_data(SRT_DATA_MSG_PTR data_ptr);

private:
    std::shared_ptr<std::thread> _thread_ptr;
    std::mutex _mutex;
    std::condition_variable_any _notify_cond;
    std::queue<SRT_DATA_MSG_PTR> _msg_queue;

    std::unordered_map<std::string, RTMP_CLIENT_PTR> _rtmp_client_map;
    bool _run_flag;
};

typedef std::shared_ptr<srt2rtmp> SRT2RTMP_PTR;

#endif