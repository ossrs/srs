/**
g++ -o ts_info ts_info.cpp -g -O0 -ansi
*/
#if 1
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define trace(msg, ...) printf(msg"\n", ##__VA_ARGS__);
#define srs_freep(p) delete p; p = NULL
#define srs_freepa(p) delete[] p; p = NULL
#define srs_assert(p) assert(p)

#endif
/**
ISO/IEC 13818-1:2000(E)
Introduction
    Intro. 1 Transport Stream
    Intro. 2 Program Stream
    Intro. 4 Packetized Elementary Stream
SECTION 2 ¨C TECHNICAL ELEMENTS
    2.4 Transport Stream bitstream requirements
        2.4.1 Transport Stream coding structure and parameters
        2.4.2 Transport Stream system target decoder
        2.4.3 Specification of the Transport Stream syntax and semantics
            2.4.3.1 Transport Stream
            2.4.3.2 Transport Stream packet layer
            2.4.3.3 Semantic definition of fields in Transport Stream packet layer
            2.4.3.5 Semantic definition of fields in adaptation field
            2.4.3.6 PES packet
            2.4.3.7 Semantic definition of fields in PES packet
        2.4.4 Program specific information
            2.4.4.5 Semantic definition of fields in program association section
            2.4.4.6 Conditional access Table
    2.5 Program Stream bitstream requirements
    2.6 Program and program element descriptors
    2.7 Restrictions on the multiplexed stream semantics
Annex A ¨C CRC Decoder Model
*/
#if 1
// Transport Stream packets are 188 bytes in length.
#define TS_PACKET_SIZE          188

// Program Association Table(see Table 2-25).
#define PID_PAT                 0x00
// Conditional Access Table (see Table 2-27).
#define PID_CAT                 0x01
// Transport Stream Description Table
#define PID_TSDT                0x02
// null packets (see Table 2-3)
#define PID_NULL                0x01FFF

/*adaptation_field_control*/
// No adaptation_field, payload only
#define AFC_PAYLOAD_ONLY        0x01
// Adaptation_field only, no payload
#define AFC_ADAPTION_ONLY       0x02
// Adaptation_field followed by payload
#define AFC_BOTH                0x03
#endif

struct TSPacket
{
    // 4B ts packet header.
    struct Header 
    {
        // 1B
        int8_t sync_byte; //8bits
        // 2B
        int8_t transport_error_indicator; //1bit
        int8_t payload_unit_start_indicator; //1bit
        int8_t transport_priority; //1bit
        u_int16_t pid; //13bits
        // 1B
        int8_t transport_scrambling_control; //2bits
        int8_t adaption_field_control; //2bits
        u_int8_t continuity_counter; //4bits
        
        int get_size()
        {
            return 4;
        }
        
        int demux(TSPacket* ppkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p)
        {
            int ret = 0;
    
            // ts packet header.
            sync_byte = *p++;
            if (sync_byte != 0x47) {
                trace("ts+sync_bytes invalid sync_bytes: %#x, expect is 0x47", sync_byte);
                return -1;
            }
            
            pid = 0;
            ((char*)&pid)[1] = *p++;
            ((char*)&pid)[0] = *p++;
            
            transport_error_indicator = (pid >> 15) & 0x01;
            payload_unit_start_indicator = (pid >> 14) & 0x01;
            transport_priority = (pid >> 13) & 0x01;
            pid &= 0x1FFF;
            
            continuity_counter = *p++;
            
            transport_scrambling_control = (continuity_counter >> 6) & 0x03;
            adaption_field_control = (continuity_counter >> 4) & 0x03;
            continuity_counter &= 0x0F;
            
            trace("ts+header sync: %#x error: %d unit_start: %d priotiry: %d pid: %d scrambling: %d adaption: %d counter: %d",
                sync_byte, transport_error_indicator, payload_unit_start_indicator, transport_priority, pid,
                transport_scrambling_control, adaption_field_control, continuity_counter);
                
            return ret;
        }
    }header;
    
    // variant ts packet adation field.
    struct AdaptionField 
    {
        // 1B
        u_int8_t adaption_field_length; //8bits
        // 1B
        int8_t discontinuity_indicator; //1bit
        int8_t random_access_indicator; //1bit
        int8_t elementary_stream_priority_indicator; //1bit
        int8_t PCR_flag; //1bit
        int8_t OPCR_flag; //1bit
        int8_t splicing_point_flag; //1bit
        int8_t transport_private_data_flag; //1bit
        int8_t adaptation_field_extension_flag; //1bit
        
        // if PCR_flag, 6B
        int64_t program_clock_reference_base; //33bits
        //6bits reserved.
        int16_t program_clock_reference_extension; //9bits
        
        // if OPCR_flag, 6B
        int64_t original_program_clock_reference_base; //33bits
        //6bits reserved.
        int16_t original_program_clock_reference_extension; //9bits
        
        // if splicing_point_flag, 1B
        int8_t splice_countdown; //8bits
        
        // if transport_private_data_flag, 1+p[0] B
        u_int8_t transport_private_data_length; //8bits
        char* transport_private_data; //[transport_private_data_length]bytes
        
        // if adaptation_field_extension_flag, 2+x bytes
        u_int8_t adaptation_field_extension_length; //8bits
        int8_t ltw_flag; //1bit
        int8_t piecewise_rate_flag; //1bit
        int8_t seamless_splice_flag; //1bit
        //5bits reserved
        // if ltw_flag, 2B
        int8_t ltw_valid_flag; //1bit
        int16_t ltw_offset; //15bits
        // if piecewise_rate_flag, 3B
        //2bits reserved
        int32_t piecewise_rate; //22bits
        // if seamless_splice_flag, 5B
        int8_t splice_type; //4bits
        int8_t DTS_next_AU0; //3bits
        int8_t marker_bit0; //1bit
        int16_t DTS_next_AU1; //15bits
        int8_t marker_bit1; //1bit
        int16_t DTS_next_AU2; //15bits
        int8_t marker_bit2; //1bit
        // left bytes.
        char* af_ext_reserved;
        
        // left bytes.
        char* af_reserved;
        
        // user defined data size.
        int __user_size;
        
        AdaptionField()
        {
            transport_private_data = NULL;
            af_ext_reserved = NULL;
            af_reserved = NULL;
            
            __user_size = 0;
        }
        
        virtual ~AdaptionField()
        {
            srs_freepa(transport_private_data);
            srs_freepa(af_ext_reserved);
            srs_freepa(af_reserved);
        }
        
        int get_size()
        {
            return __user_size;
        }
        
        int demux(TSPacket* ppkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p)
        {
            int ret = 0;
    
            adaption_field_length = *p++;
            u_int8_t* pos_af = p;
            __user_size = 1 + adaption_field_length;
            
            if (adaption_field_length <= 0) {
                trace("ts+af empty af decoded.");
                return ret;
            }
            
            int8_t value = *p++;
            
            discontinuity_indicator              =   (value >> 7) & 0x01;
            random_access_indicator              =   (value >> 6) & 0x01;
            elementary_stream_priority_indicator =   (value >> 5) & 0x01;
            PCR_flag                             =   (value >> 4) & 0x01;
            OPCR_flag                            =   (value >> 3) & 0x01;
            splicing_point_flag                  =   (value >> 2) & 0x01;
            transport_private_data_flag          =   (value >> 1) & 0x01;
            adaptation_field_extension_flag      =   (value >> 0) & 0x01;
            
            trace("ts+af af flags parsed, discontinuity: %d random: %d priority: %d PCR: %d OPCR: %d slicing: %d private: %d extension: %d",
                discontinuity_indicator, random_access_indicator, elementary_stream_priority_indicator, PCR_flag, OPCR_flag, splicing_point_flag,
                transport_private_data_flag, adaptation_field_extension_flag);
            
            char* pp = NULL;
            if (PCR_flag) {
                pp = (char*)&program_clock_reference_base;
                pp[5] = *p++;
                pp[4] = *p++;
                pp[3] = *p++;
                pp[2] = *p++;
                pp[1] = *p++;
                pp[0] = *p++;
                
                program_clock_reference_extension = program_clock_reference_base & 0x1F;
                program_clock_reference_base = (program_clock_reference_base >> 9) & 0x1FFFFFFFF;
            }
            if (OPCR_flag) {
                pp = (char*)&original_program_clock_reference_base;
                pp[5] = *p++;
                pp[4] = *p++;
                pp[3] = *p++;
                pp[2] = *p++;
                pp[1] = *p++;
                pp[0] = *p++;
                
                original_program_clock_reference_extension = original_program_clock_reference_base & 0x1F;
                original_program_clock_reference_base = (original_program_clock_reference_base >> 9) & 0x1FFFFFFFF;
            }
            if (splicing_point_flag) {
                splice_countdown = *p++;
            }
            if (transport_private_data_flag) {
                transport_private_data_length = *p++;
                transport_private_data = new char[transport_private_data_length];
                for (int i = 0; i < transport_private_data_length; i++) {
                    transport_private_data[i] = *p++;
                }
            }
            if (adaptation_field_extension_flag) {
                adaptation_field_extension_length = *p++;
                u_int8_t* pos_af_ext = p;
                
                ltw_flag = *p++;
                
                piecewise_rate_flag = (ltw_flag >> 6) & 0x01;
                seamless_splice_flag = (ltw_flag >> 5) & 0x01;
                ltw_flag = (ltw_flag >> 7) & 0x01;
                
                if (ltw_flag) {
                    pp = (char*)&ltw_offset;
                    pp[1] = *p++;
                    pp[0] = *p++;
                    
                    ltw_valid_flag = (ltw_offset >> 15) &0x01;
                    ltw_offset &= 0x7FFF;
                }
                if (piecewise_rate_flag) {
                    pp = (char*)&piecewise_rate;
                    pp[2] = *p++;
                    pp[1] = *p++;
                    pp[0] = *p++;
                    
                    piecewise_rate &= 0x3FFFFF;
                }
                if (seamless_splice_flag) {
                    // 1B
                    marker_bit0 = *p++;
                    
                    splice_type = (marker_bit0 >> 4) & 0x0F;
                    DTS_next_AU0 = (marker_bit0 >> 1) & 0x07;
                    marker_bit0 &= 0x01;
                    
                    // 2B
                    pp = (char*)&DTS_next_AU1;
                    pp[1] = *p++;
                    pp[0] = *p++;
                    
                    marker_bit1 = DTS_next_AU1 & 0x01;
                    DTS_next_AU1 = (DTS_next_AU1 >> 1) & 0x7FFF;
                    
                    // 2B
                    pp = (char*)&DTS_next_AU2;
                    pp[1] = *p++;
                    pp[0] = *p++;
                    
                    marker_bit2 = DTS_next_AU2 & 0x01;
                    DTS_next_AU2 = (DTS_next_AU2 >> 1) & 0x7FFF;
                }
                
                // af_ext_reserved
                int ext_size = adaptation_field_extension_length - (p - pos_af_ext);
                if (ext_size > 0) {
                    af_ext_reserved = new char[ext_size];
                    memcpy(af_ext_reserved, p, ext_size);
                    p += ext_size;
                }
            }
            
            // af_reserved
            int af_size = adaption_field_length - (p - pos_af);
            if (af_size > 0) {
                af_reserved = new char[af_size];
                memcpy(af_reserved, p, af_size);
                p += af_size;
            }
            
            return ret;
        }
    }adaption_field;
    
    // variant ts packet payload.
    // PES packet or PSI table.
    struct Payload 
    {
        /**
        * the size of payload(payload plush the 1byte pointer_field).
        */
        int size;
        int pointer_field_size;
        
        /**
        * the actually parsed type.
        */
        enum Type 
        {
            TypeUnknown=-1, 
            TypeReserved, // TypeReserved, nothing parsed, used reserved.
            TypePAT, //TypePAT, PAT parsed, in pat field.
        } type;
        
        /**
        * 2.4.4.2 Semantics definition of fields in pointer syntax
        */
        u_int8_t pointer_field;
        
        /**
        * if not parsed, store data in this field.
        */
        struct Reserved
        {
            int size;
            char* bytes;
            
            Reserved()
            {
                size = 0;
                bytes = NULL;
            }
            
            virtual ~Reserved()
            {
                srs_freepa(bytes);
            }
        
            int demux(TSPacket* ppkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p)
            {
                int ret = 0;
                
                size = ppkt->payload.size - ppkt->payload.pointer_field_size;
            
                // not parsed bytes.
                if (size > 0) {
                    bytes = new char[size];
                    memcpy(bytes, p, size);
                    p += size;
                }
            
                return ret;
            }
        } *reserved;
        
        /**
        * 2.4.4.3 Program association Table. page 61.
        */
        struct PAT {
            // 1B
            u_int8_t table_id; //8bits
            
            // 2B
            int8_t section_syntax_indicator; //1bit
            int8_t const0_value; //1bit
            // 2bits reserved. 
            u_int16_t section_length; //12bits
            
            // 2B
            u_int16_t transport_stream_id; //16bits
            
            // 1B
            // 2bits reerverd.
            int8_t version_number; //5bits
            int8_t current_next_indicator; //1bit
            
            // 1B
            u_int8_t section_number; //8bits
            
            // 1B
            u_int8_t last_section_number; //8bits
            
            // multiple 4B program data.
            // program_number 16bits
            // reserved 2bits
            // 13bits data: 0x1FFF
            // if program_number program_map_PID 13bits
            // else network_PID 13bytes.
            int program_size;
            int32_t* programs; //32bits
            
            // 4B
            int32_t CRC_32; //32bits
            
            PAT()
            {
                programs = NULL;
            }
            
            virtual ~PAT()
            {
                srs_freepa(programs);
            }
            
            int get_program(int index)
            {
                srs_assert(index < program_size);
                return programs[index] & 0x1FFF;
            }
            
            int demux(TSPacket* ppkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p)
            {
                int ret = 0;
                
                table_id = *p++;
                
                char* pp = (char*)&section_length;
                pp[1] = *p++;
                pp[0] = *p++;
                u_int8_t* pos = p;
                
                section_syntax_indicator = (section_length >> 15) & 0x01;
                const0_value = (section_length >> 14) & 0x01;
                section_length &= 0x0FFF;
                
                pp = (char*)&transport_stream_id;
                pp[1] = *p++;
                pp[0] = *p++;
                
                current_next_indicator = *p++;
                version_number = (current_next_indicator >> 1) & 0x1F;
                current_next_indicator &= 0x01;
                
                section_number = *p++;
                last_section_number = *p++;
                
                // 4 is crc size.
                int program_bytes = section_length - 4 - (p - pos);
                program_size = program_bytes / 4;
                if (program_size > 0) {
                    programs = new int32_t[program_size];
                    for (int i = 0; i < program_size; i++) {
                        pp = (char*)&programs[i];
                        pp[3] = *p++;
                        pp[2] = *p++;
                        pp[1] = *p++;
                        pp[0] = *p++;
                    }
                }
                
                pp = (char*)&CRC_32;
                pp[3] = *p++;
                pp[2] = *p++;
                pp[1] = *p++;
                pp[0] = *p++;
                
                return ret;
            }
        } *pat;
        
        /**
        * 2.4.3.6 PES packet. page 49.
        */
        
        Payload()
        {
            size = 0;
            pointer_field_size = 0;
            
            type = TypeUnknown;
            reserved = NULL;
            pat = NULL;
        }
        
        virtual ~Payload()
        {
            srs_freep(reserved);
            srs_freep(pat);
        }
        
        int demux(TSPacket* ppkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p)
        {
            int ret = 0;
            
            if (ppkt->header.payload_unit_start_indicator) {
                pointer_field = *p++;
                pointer_field_size = 1;
            }
            
            if (ppkt->header.pid == PID_PAT) {
                type = TypePAT;
                pat = new PAT();
                return pat->demux(ppkt, start, last, p);
            }
            
            // not parsed bytes.
            type = TypeReserved;
            reserved = new Reserved();
            if ((ret = reserved->demux(ppkt, start, last, p)) != 0) {
                return ret;
            }
            
            return ret;
        }
    }payload;
    
    int demux(u_int8_t* start, u_int8_t* last, u_int8_t*& p)
    {
        int ret = 0;
        
        if ((ret = header.demux(this, start, last, p))  != 0) {
            return ret;
        }
    
        if (header.adaption_field_control == AFC_ADAPTION_ONLY || header.adaption_field_control == AFC_BOTH) {
            if ((ret = adaption_field.demux(this, start, last, p)) != 0) {
                trace("ts+header af(adaption field) decode error. ret=%d", ret);
                return ret;
            }
            trace("ts+header af(adaption field decoded.");
        }
        
        // calc the user defined data size for payload.
        payload.size = TS_PACKET_SIZE - header.get_size() - adaption_field.get_size();
        
        if (header.adaption_field_control == AFC_PAYLOAD_ONLY || header.adaption_field_control == AFC_BOTH) {
            if ((ret = payload.demux(this, start, last, p)) != 0) {
                trace("ts+header payload decode error. ret=%d", ret);
                return ret;
            }
            trace("ts+header payload decoded.");
        }
        
        trace("ts+header parsed finished. parsed: %d left: %d header: %d payload: %d(%d+%d)", 
            (int)(p - start), (int)(last - p), header.get_size(), payload.size, payload.pointer_field_size, 
            payload.size - payload.pointer_field_size);
        
        return finish();
    }
    
    int finish()
    {
        return 0;
    }
};

int main(int /*argc*/, char** /*argv*/)
{
    const char* file = "livestream-1347.ts";
    //file = "nginx-rtmp-hls/livestream-1347-currupt.ts";
    int fd = open(file, O_RDONLY);
    
    int ret = 0;
    trace("demuxer+read packet count offset T+0  T+1  T+2  T+3  T+x T+L2 T+L1 T+L0");
    for (int i = 0, offset = 0; ; i++) {
        u_int8_t ts_packet[TS_PACKET_SIZE];
        memset(ts_packet, 0, sizeof(ts_packet));
        
        int nread = read(fd, ts_packet, sizeof(ts_packet));
        if (nread == 0) {
            trace("demuxer+read got EOF, read completed, offset: %07d.", offset);
            break;
        }
        if (nread != TS_PACKET_SIZE) {
            trace("demuxer+read error to read ts packet. nread=%d", nread);
            break;
        }
        trace("demuxer+read packet %04d %07d 0x%02x 0x%02x 0x%02x 0x%02x ... 0x%02x 0x%02x 0x%02x", 
            i, offset, ts_packet[0], ts_packet[1], ts_packet[2], ts_packet[3], 
            ts_packet[TS_PACKET_SIZE - 3], ts_packet[TS_PACKET_SIZE - 2], ts_packet[TS_PACKET_SIZE - 1]);
        
        u_int8_t* p = ts_packet;
        u_int8_t* start = ts_packet;
        u_int8_t* last = ts_packet + TS_PACKET_SIZE;
        
        TSPacket pkt;
        if ((ret = pkt.demux(start, last, p)) != 0) {
            trace("demuxer+read decode ts packet error. ret=%d", ret);
            return ret;
        }
        
        offset += nread;
    }
    
    close(fd);
    return ret;
}

