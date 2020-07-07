/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Runner365
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef TS_DEMUX_H
#define TS_DEMUX_H

#include <srs_core.hpp>

#include "srt_data.hpp"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

/* mpegts stream type in ts pmt
Value    Description
0x00     ITU-T | ISO/IEC Reserved
0x01     ISO/IEC 11172-2 Video (mpeg video v1)
0x02     ITU-T Rec. H.262 | ISO/IEC 13818-2 Video(mpeg video v2)or ISO/IEC 11172-2 constrained parameter video stream
0x03     ISO/IEC 11172-3 Audio (MPEG 1 Audio codec Layer I, Layer II and Layer III audio specifications) 
0x04     ISO/IEC 13818-3 Audio (BC Audio Codec) 
0x05     ITU-T Rec. H.222.0 | ISO/IEC 13818-1 private_sections 
0x06     ITU-T Rec. H.222.0 | ISO/IEC 13818-1 PES packets containing private data 
0x07     ISO/IEC 13522 MHEG 
0x08     ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A DSM-CC 
0x09     ITU-T Rec. H.222.1 
0x0A     ISO/IEC 13818-6 type A 
0x0B     ISO/IEC 13818-6 type B 
0x0C     ISO/IEC 13818-6 type C 
0x0D     ISO/IEC 13818-6 type D 
0x0E     ITU-T Rec. H.222.0 | ISO/IEC 13818-1 auxiliary 
0x0F     ISO/IEC 13818-7 Audio with ADTS transport syntax 
0x10     ISO/IEC 14496-2 Visual 
0x11     ISO/IEC 14496-3 Audio with the LATM transport syntax as defined in ISO/IEC 14496-3/Amd.1 
0x12     ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in PES packets 
0x13     ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in ISO/IEC 14496_sections 
0x14     ISO/IEC 13818-6 Synchronized Download Protocol 
0x15     Metadata carried in PES packets 
0x16     Metadata carried in metadata_sections 
0x17     Metadata carried in ISO/IEC 13818-6 Data Carousel 
0x18     Metadata carried in ISO/IEC 13818-6 Object Carousel 
0x19     Metadata carried in ISO/IEC 13818-6 Synchronized Download Protocol 
0x1A     IPMP stream (defined in ISO/IEC 13818-11, MPEG-2 IPMP) 
0x1B     AVC video stream as defined in ITU-T Rec. H.264 | ISO/IEC 14496-10 Video (h.264) 
0x1C     ISO/IEC 14496-3 Audio, without using any additional transport syntax, such as DST, ALS and SLS 
0x1D     ISO/IEC 14496-17 Text 
0x1E     Auxiliary video stream as defined in ISO/IEC 23002-3 (AVS) 
0x1F-0x7E ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved 
0x7F     IPMP stream 0x80-0xFF User Private
*/
#define STREAM_TYPE_VIDEO_MPEG1     0x01
#define STREAM_TYPE_VIDEO_MPEG2     0x02
#define STREAM_TYPE_AUDIO_MPEG1     0x03
#define STREAM_TYPE_AUDIO_MPEG2     0x04
#define STREAM_TYPE_PRIVATE_SECTION 0x05
#define STREAM_TYPE_PRIVATE_DATA    0x06
#define STREAM_TYPE_AUDIO_AAC       0x0f
#define STREAM_TYPE_AUDIO_AAC_LATM  0x11
#define STREAM_TYPE_VIDEO_MPEG4     0x10
#define STREAM_TYPE_METADATA        0x15
#define STREAM_TYPE_VIDEO_H264      0x1b
#define STREAM_TYPE_VIDEO_HEVC      0x24
#define STREAM_TYPE_VIDEO_CAVS      0x42
#define STREAM_TYPE_VIDEO_VC1       0xea
#define STREAM_TYPE_VIDEO_DIRAC     0xd1

#define STREAM_TYPE_AUDIO_AC3       0x81
#define STREAM_TYPE_AUDIO_DTS       0x82
#define STREAM_TYPE_AUDIO_TRUEHD    0x83
#define STREAM_TYPE_AUDIO_EAC3      0x87

class ts_media_data_callback_I {
public:
    virtual void on_data_callback(SRT_DATA_MSG_PTR data_ptr, unsigned int media_type, uint64_t dts, uint64_t pts) = 0;
};

typedef std::shared_ptr<ts_media_data_callback_I> TS_DATA_CALLBACK_PTR;

class adaptation_field {
public:
    adaptation_field(){};
    ~adaptation_field(){};

public:
    unsigned char _adaptation_field_length;

    unsigned char _discontinuity_indicator:1;
    unsigned char _random_access_indicator:1;
    unsigned char _elementary_stream_priority_indicator:1;
    unsigned char _PCR_flag:1;
    unsigned char _OPCR_flag:1;
    unsigned char _splicing_point_flag:1;
    unsigned char _transport_private_data_flag:1;
    unsigned char _adaptation_field_extension_flag:1;
    
    //if(PCR_flag == '1')
    unsigned long _program_clock_reference_base;//33 bits
    unsigned short _program_clock_reference_extension;//9bits
    //if (OPCR_flag == '1')
    unsigned long _original_program_clock_reference_base;//33 bits
    unsigned short _original_program_clock_reference_extension;//9bits
    //if (splicing_point_flag == '1')
    unsigned char _splice_countdown;
    //if (transport_private_data_flag == '1') 
    unsigned char _transport_private_data_length;
    unsigned char _private_data_byte[256];
    //if (adaptation_field_extension_flag == '1')
    unsigned char _adaptation_field_extension_length;
    unsigned char _ltw_flag;
    unsigned char _piecewise_rate_flag;
    unsigned char _seamless_splice_flag;
    unsigned char _reserved0;
    //if (ltw_flag == '1')
    unsigned short _ltw_valid_flag:1;
    unsigned short _ltw_offset:15;
    //if (piecewise_rate_flag == '1')
    unsigned int _piecewise_rate;//22bits
    //if (seamless_splice_flag == '1')
    unsigned char _splice_type;//4bits
    unsigned char _DTS_next_AU1;//3bits
    unsigned char _marker_bit1;//1bit
    unsigned short _DTS_next_AU2;//15bit
    unsigned char _marker_bit2;//1bit
    unsigned short _DTS_next_AU3;//15bit
};

class ts_header {
public:
    ts_header(){}
    ~ts_header(){}

public:
    unsigned char _sync_byte;

    unsigned short _transport_error_indicator:1;
    unsigned short _payload_unit_start_indicator:1;
    unsigned short _transport_priority:1;
    unsigned short _PID:13;

    unsigned char _transport_scrambling_control:2;
    unsigned char _adaptation_field_control:2;
    unsigned char _continuity_counter:4;

    adaptation_field _adaptation_field_info;
};

typedef struct {
    unsigned short _program_number;
    unsigned short _pid;
    unsigned short _network_id;
} PID_INFO;

class pat_info {
public:
    pat_info(){};
    ~pat_info(){};

public:
    unsigned char _table_id;

    unsigned short _section_syntax_indicator:1;
    unsigned short _reserved0:1;
    unsigned short _reserved1:2;
    unsigned short _section_length:12;

    unsigned short _transport_stream_id;

    unsigned char _reserved3:2;
    unsigned char _version_number:5;
    unsigned char _current_next_indicator:1;

    unsigned char _section_number;
    unsigned char _last_section_number;
    std::vector<PID_INFO> _pid_vec;
};

typedef struct {
    unsigned char  _stream_type;
    unsigned short _reserved1:3;
    unsigned short _elementary_PID:13;
    unsigned short _reserved:4;
    unsigned short _ES_info_length;
    unsigned char  _dscr[4096];
    unsigned int   _crc_32;
} STREAM_PID_INFO;

class pmt_info {
public:
    pmt_info(){};
    ~pmt_info(){};
public:
    unsigned char _table_id;
    unsigned short _section_syntax_indicator:1;
    unsigned short _reserved1:1;
    unsigned short _reserved2:2;
    unsigned short _section_length:12;
    unsigned short _program_number:16;
    unsigned char  _reserved:2;
    unsigned char  _version_number:5;
    unsigned char  _current_next_indicator:5;
    unsigned char  _section_number;
    unsigned char  _last_section_number;
    unsigned short _reserved3:3;
    unsigned short _PCR_PID:13;
    unsigned short _reserved4:4;
    unsigned short _program_info_length:12;
    unsigned char  _dscr[4096];

    std::unordered_map<unsigned short, unsigned char> _pid2steamtype;
    std::vector<STREAM_PID_INFO> _stream_pid_vec;
};

class ts_demux {
public:
    ts_demux();
    ~ts_demux();

    int decode(SRT_DATA_MSG_PTR data_ptr, TS_DATA_CALLBACK_PTR callback);

private:
    int decode_unit(unsigned char* data_p, std::string key_path, TS_DATA_CALLBACK_PTR callback);
    bool is_pmt(unsigned short pmt_id);
    int pes_parse(unsigned char* p, size_t npos, unsigned char** ret_pp, size_t& ret_size,
            uint64_t& dts, uint64_t& pts);
    void insert_into_databuf(unsigned char* data_p, size_t data_size, std::string key_path, unsigned short pid);
    void on_callback(TS_DATA_CALLBACK_PTR callback, unsigned short pid,
                std::string key_path, uint64_t dts, uint64_t pts);

private:
    std::string _key_path;//only for srt

    pat_info _pat;
    pmt_info _pmt;
    std::vector<SRT_DATA_MSG_PTR> _data_buffer_vec;
    size_t _data_total;
    unsigned short _last_pid;
    uint64_t _last_dts;
    uint64_t _last_pts;
};

typedef std::shared_ptr<ts_demux> TS_DEMUX_PTR;

#endif