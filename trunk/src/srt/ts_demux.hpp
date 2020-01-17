#ifndef TS_DEMUX_H
#define TS_DEMUX_H
#include "srt_data.hpp"
#include <string>
#include <memory>
#include <vector>

class ts_media_data_callback_I {
public:
    virtual void on_data_callback(SRT_DATA_MSG_PTR data_ptr);
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
    pat_info();
    ~pat_info();

public:
    unsigned char _table_id;

    unsigned short _section_syntax_indicator:1;
    unsigned short _reserved0:1;
    unsigned short _reserved1:2;
    unsigned short _section_length:12;

    unsigned short transport_stream_id;

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
private:
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

    std::vector<STREAM_PID_INFO> _stream_pid_vec;
};

class ts_demux {
public:
    ts_demux();
    ~ts_demux();

    int decode(SRT_DATA_MSG_PTR data_ptr, TS_DATA_CALLBACK_PTR callback);

private:
    int decode_ts_header(unsigned char* data_p, ts_header& ts_header_info);
    bool is_pmt(unsigned short pmt_id);

private:
    std::string _key_path;//only for srt

    pat_info _pat;
    pmt_info _pmt;
};

#endif