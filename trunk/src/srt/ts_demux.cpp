#include "ts_demux.hpp"
#include <assert.h>

ts_demux::ts_demux() {

}

ts_demux::~ts_demux() {

}

int ts_demux::decode(SRT_DATA_MSG_PTR data_ptr, TS_DATA_CALLBACK_PTR callback) {

    return 0;
}

int decode_ts_header(unsigned char* data_p, ts_header& ts_header_info) {
    int pos = 0;
    int npos = 0;

    ts_header_info._sync_byte = data_p[pos];
    pos++;

    ts_header_info._transport_error_indicator = (data_p[pos]&0x80)>>7;
    ts_header_info._payload_unit_start_indicator = (data_p[pos]&0x40)>>6;
    ts_header_info._transport_priority = (data_p[pos]&0x20)>>5;
    ts_header_info._PID = ((data_p[pos]<<8)|data_p[pos+1])&0x1FFF;
    pos += 2;
    
    ts_header_info._transport_scrambling_control = (data_p[pos]&0xC0)>>6;
    ts_header_info._adaptation_field_control = (data_p[pos]&0x30)>>4;
    ts_header_info._continuity_counter = (data_p[pos]&0x0F);
    pos++;
    npos = pos;

    adaptation_field* field_p = &(ts_header_info._adaptation_field_info);
    if( ts_header_info._adaptation_field_control == 2 
        || ts_header_info._adaptation_field_control == 3 ){
        // adaptation_field()
        field_p->_adaptation_field_length = data_p[pos];
        pos++;

        if( field_p->_adaptation_field_length > 0 ){
            field_p->_discontinuity_indicator = (data_p[pos]&0x80)>>7;
            field_p->_random_access_indicator = (data_p[pos]&0x40)>>6;
            field_p->_elementary_stream_priority_indicator = (data_p[pos]&0x20)>>5;
            field_p->_PCR_flag = (data_p[pos]&0x10)>>4;
            field_p->_OPCR_flag = (data_p[pos]&0x08)>>3;
            field_p->_splicing_point_flag = (data_p[pos]&0x04)>>2;
            field_p->_transport_private_data_flag = (data_p[pos]&0x02)>>1;
            field_p->_adaptation_field_extension_flag = (data_p[pos]&0x01);
            pos++;

            if( field_p->_PCR_flag == 1 ) { // PCR info
                //program_clock_reference_base 33 uimsbf
                //reserved 6 bslbf
                //program_clock_reference_extension 9 uimsbf
                pos += 6;
            }
            if( field_p->_OPCR_flag == 1 ) {
                //original_program_clock_reference_base 33 uimsbf
                //reserved 6 bslbf
                //original_program_clock_reference_extension 9 uimsbf
                pos += 6;
            }
            if( field_p->_splicing_point_flag == 1 ) {
                //splice_countdown 8 tcimsbf
                pos++;
            }
            if( field_p->_transport_private_data_flag == 1 ) {
                //transport_private_data_length 8 uimsbf
                field_p->_transport_private_data_length = data_p[pos];
                pos++;
                memcpy(field_p->_private_data_byte, data_p + pos, field_p->_transport_private_data_length);
            }
            if( field_p->_adaptation_field_extension_flag == 1 ) {
                //adaptation_field_extension_length 8 uimsbf
                field_p->_adaptation_field_extension_length = data_p[pos];
                pos++;
                //ltw_flag 1 bslbf
                field_p->_ltw_flag = (data_p[pos]&0x80)>>7;
                //piecewise_rate_flag 1 bslbf
                field_p->_piecewise_rate_flag = (data_p[pos]&0x40)>>6;
                //seamless_splice_flag 1 bslbf
                field_p->_seamless_splice_flag = (data_p[pos]&0x20)>>5;
                //reserved 5 bslbf
                pos++;
                if (field_p->_ltw_flag == 1) {
                    //ltw_valid_flag 1 bslbf
                    //ltw_offset 15 uimsbf
                    pos += 2;
                }
                if (field_p->_piecewise_rate_flag == 1) {
                    //reserved 2 bslbf
                    //piecewise_rate 22 uimsbf
                    pos += 3;
                }
                if (field_p->_seamless_splice_flag == 1) {
                    //splice_type 4 bslbf
                    //DTS_next_AU[32..30] 3 bslbf
                    //marker_bit 1 bslbf
                    //DTS_next_AU[29..15] 15 bslbf
                    //marker_bit 1 bslbf
                    //DTS_next_AU[14..0] 15 bslbf
                    //marker_bit 1 bslbf
                    pos += 5;
                }
            }
        }
        
        
        npos += sizeof(field_p->_adaptation_field_length) + field_p->_adaptation_field_length;
        assert(pos == npos);
    }

    if(ts_header_info._adaptation_field_control == 1 
       || ts_header_info._adaptation_field_control == 3 ) {
        // data_byte with placeholder
        // payload parser
        if(ts_header_info._PID == 0x00){
            // PAT // program association table
            if(ts_header_info._payload_unit_start_indicator) {
                pos++;
            }
            _pat._table_id = data_p[pos];
            pos++;
            _pat._section_syntax_indicator = (data_p[pos]>>7)&0x01;
            // skip 3 bits of 1 zero and 2 reserved
            _pat._section_length = ((data_p[pos]<<8)|data_p[pos+1])&0x0FFF;
            pos += 2;
            _pat._transport_stream_id = (data_p[pos]<<8)|data_p[pos+1];
            pos += 2;
            // reserved 2 bits
            _pat._version_number = (data_p[pos]&0x3E)>>1;
            _pat._current_next_indicator = data_p[pos]&0x01;
            pos++;
            _pat._section_number = data_p[pos];
            pos++;
            _pat._last_section_number = data_p[pos];
            assert(table_id == 0x00);
            assert(188-npos>section_length+3); // PAT = section_length + 3
            pos++;
            _pat._pid_vec.clear();
            for (;pos+4 <= section_length-5-4+9;) { // 4:CRC, 5:follow section_length item  rpos + 4(following unit length) section_length + 9(above field and unit_start_first_byte )
                PID_INFO pid_info;
                //program_number 16 uimsbf
                pid_info._program_number = data_p[pos]<<8|data_p[pos+1];
                pos += 2;
//              reserved 3 bslbf
                
                if (pid_info._program_number == 0) {
//                  // network_PID 13 uimsbf
                    pid_info._network_id = (data_p[pos]<<8|data_p[pos+1])&0x1FFF;
                    pos += 2;
                }
                else {
//                  //     program_map_PID 13 uimsbf
                    pid_info._pid = (data_p[pos]<<8|data_p[pos+1])&0x1FFF;
                    pos += 2;
                }
                _pat._pid_vec.push_back(pid_info);
                // network_PID and program_map_PID save to list
            }
//               CRC_32 use pat to calc crc32, eq
            pos += 4;
        }else if(ts_header_info._PID == 0x01){
            // CAT // conditional access table
        }else if(ts_header_info._PID == 0x02){
            //TSDT  // transport stream description table
        }else if(ts_header_info._PID == 0x03){
            //IPMP // IPMP control information table
            // 0x0004-0x000F Reserved
            // 0x0010-0x1FFE May be assigned as network_PID, Program_map_PID, elementary_PID, or for other purposes
        }else if(ts_header_info._PID == 0x11){
            // SDT // https://en.wikipedia.org/wiki/Service_Description_Table / https://en.wikipedia.org/wiki/MPEG_transport_stream
        }else if(is_pmt(ts_header_info._PID)) {
            int rpos = 0;
            if(ts_header_info._payload_unit_start_indicator)
                rpos++;
            _pmt._table_id = data_p[rpos];
            pos++;
            _pmt._section_syntax_indicator = (data_p[rpos]>>7)&0x01;
            // skip 3 bits of 1 zero and 2 reserved
            _pmt._section_length = ((data_p[rpos]<<8)|data_p[pos+1])&0x0FFF;
            pos += 2;
            _pmt._program_number = (data_p[rpos]<<8)|data_p[rpos+1];
            pos += 2;
            // reserved 2 bits
            _pmt._version_number = (data_p[pos]&0x3E)>>1;
            _pmt._current_next_indicator = data_p[pos]&0x01;
            pos++;
            _pmt._section_number = data_p[pos];
            pos++;
            _pmt._last_section_number = data_p[pos];
            pos++;
            // skip 3 bits for reserved 3 bslbf
            _pmt._PCR_PID = ((data_p[pos]<<8)|data_p[pos+1])&0x1FFF; //PCR_PID 13 uimsbf
            pos += 2;

            //reserved 4 bslbf
            _pmt._program_info_length = ((data_p[pos]<<8)|data_p[pos+1])&0x0FFF;//program_info_length 12 uimsbf
            pos += 2;
            assert(_pmt._table_id==0x02); //  0x02, // TS_program_map_section
            memcpy(_pmt._dscr, data_p+pos, _pmt._program_info_length);
//               for (i = 0; i < N; i++) {
//                   descriptor()
//               }
            pos += _pmt._program_info_length;
            _pmt._stream_pid_vec.clear();
            for (; rpos + 5 <= _pmt._section_number + 4 - 4; ) { // rpos(above field length) i+5(following unit length) section_length +3(PMT begin three bytes)+1(payload_unit_start_indicator) -4(crc32)
                STREAM_PID_INFO pid_info;
                pid_info._stream_type = data_p[pos];//stream_type 8 uimsbf  0x1B AVC video stream as defined in ITU-T Rec. H.264 | ISO/IEC 14496-10 Video
                pos++;
                //reserved 3 bslbf
                pid_info._elementary_PID = ((data_p[pos]<<8)|data_p[pos+1])&0x1FFF; //elementary_PID 13 uimsbf
                pos += 2;
                //reserved 4 bslbf
                pid_info._ES_info_length = ((data_p[pos]<<8)|data_p[pos+1])&0x0FFF; //ES_info_length 12 uimsbf
                pos += 2;
                if( rpos + pid_info._ES_info_length > _pmt._section_length + 4 - 4 )
                    break;
                int absES_info_length = rpos + pid_info._ES_info_length;
                for (; rpos<absES_info_length; ) {
                    //descriptor()
                    int descriptor_tag = data_p[pos];
                    pos++;
                    int descriptor_length = data_p[pos];
                    pos++;
                    memcpy(pid_info._dscr, data_p + pos, descriptor_length);
                    pos += descriptor_length;
                }
                // save program_number(stream num) elementary_PID(PES PID) stream_type(stream codec)
                _pmt._stream_pid_vec.push_back(pid_info);
            }
            rpos += 4;//CRC_32
        }else if(ts_header_info._PID == 0x0042){
            // USER
        }else if(ts_header_info._PID == 0x1FFF){
            // Null packet
        }else{
            bool isFound = false;
            for (int i=0; i<pes_index; i++) {
                if(PID == pes_info[i].pid){
                    isFound = true;
                    printf("PES = %X \n", pes_info[i].pid);
                    int rpos = 0;
                    if(payload_unit_start_indicator){
                        rpos = pes_parse(p, rpos, npos, pes_info[i].fd);
                    }else{
                        // rpos = pes_parse(p, rpos);
                        printf("PES payload_unit_start_indicator is zero; rpos=%d npos=%d remain=%d hex=%s\n", rpos, npos, 188-(npos+rpos), hexdump(p, 8));
                        fwrite(p, 1, 188-(npos+rpos), pes_info[i].fd);
                    }
                }
            }
            if(!isFound){
                printf("unknown PID = %X \n", PID);
            }
        }
    }

    return 0;
}

bool ts_demux::is_pmt(unsigned short pid) {
    for (int index = 0; index < _pat._pid_vec.size(); index++) {
        if (_pat._pid_vec[index]._program_number != 0) {
            if (_pat._pid_vec[index]._pid == pid) {
                return true;
            }
        }
    }
    return false;
}
