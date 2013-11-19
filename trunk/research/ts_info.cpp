/**
g++ -o ts_info ts_info.cpp -g -O0 -ansi
*/
#if 1
// for int64_t print using PRId64 format.
#ifndef __STDC_FORMAT_MACROS
    #define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <vector>
#include <map>

#define trace(msg, ...) printf(msg"\n", ##__VA_ARGS__);
#define srs_freep(p) delete p; p = NULL
#define srs_freepa(p) delete[] p; p = NULL
#define srs_assert(p) assert(p)
#define srs_min(a, b) ((a)<(b)? (a):(b))

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
    	2.6.2 Video stream descriptor
		2.6.4 Audio stream descriptor
    	2.6.34 IBP descriptor
    	2.6.37 Semantic definition of fields in MPEG-4 video descriptor
    	2.6.38 MPEG-4 audio descriptor
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

// Table 2-29 â€“ Stream type assignments. page 66.
enum TSStreamType
{
	/*defined by ffmpeg*/
	TSStreamTypeVideoMpeg1 			= 0x01,
	TSStreamTypeVideoMpeg2 			= 0x02,
	TSStreamTypeAudioMpeg1 			= 0x03,
	TSStreamTypeAudioMpeg2 			= 0x04,
	TSStreamTypePrivateSection 		= 0x05,
	TSStreamTypePrivateData 		= 0x06,
	TSStreamTypeAudioAAC	 		= 0x0f,
	TSStreamTypeVideoMpeg4	 		= 0x10,
	TSStreamTypeVideoH264	 		= 0x1b,
	TSStreamTypeAudioAC3	 		= 0x81,
	TSStreamTypeAudioDTS	 		= 0x8a,
};

/**
* the actually parsed type.
*/
enum TSPidType 
{
    TSPidTypeReserved = 0, // TSPidTypeReserved, nothing parsed, used reserved.
    
    TSPidTypePAT, // Program associtate table
    TSPidTypePMT, // Program map table.
    
    TSPidTypeVideo,
    TSPidTypeAudio,
};

// forward declares.
class TSHeader;
class TSAdaptionField;
class TSPayload;
class TSPayloadReserved;
class TSPayloadPAT;
class TSPayloadPMT;
class TSPayloadPES;
class TSContext;
class TSMessage;

// TSPacket declares.
class TSPacket
{
public:
	TSHeader* header;
    TSAdaptionField* adaption_field;
    TSPayload* payload;
    
    TSPacket();
    virtual ~TSPacket();
    int demux(TSContext* ctx, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg);
    int finish();
};

/**
* 2.4.3.2 Transport Stream packet layer. page 36.
*/
class TSHeader 
{
public:
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
    
    TSHeader();
    virtual ~TSHeader();
    int get_size();
    int demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg);
};

// variant ts packet adation field. page 40.
class TSAdaptionField 
{
public:
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
    
    TSAdaptionField();
    virtual ~TSAdaptionField();
    int get_size();
    int demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg);
};

// variant ts packet payload. 
// PES packet or PSI table.
// TSPayloadPAT: page 61.
class TSPayload 
{
public:
    /**
    * the size of payload(payload plush the 1byte pointer_field).
    */
    int size;
    int pointer_field_size;
    
	TSPidType type;
    
    /**
    * 2.4.4.2 Semantics definition of fields in pointer syntax
    */
    u_int8_t pointer_field;
    
    TSPayloadReserved* reserved;
    TSPayloadPAT* pat;
    TSPayloadPMT* pmt;
    TSPayloadPES* pes;
    
    /**
    * 2.4.3.6 PES packet. page 49.
    */
    
    TSPayload();
    virtual ~TSPayload();;
    void read_pointer_field(TSPacket* pkt, u_int8_t*& p);
    int demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg);
};


/**
* 2.4.4.3 Program association Table. page 61.
*/
class TSPayloadPAT 
{
public:
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
    
    TSPayloadPAT();
    virtual ~TSPayloadPAT();
    int demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg);
};

class TSPMTESInfo
{
public:
    // 1B
    u_int8_t stream_type; //8bits
    
    // 2B
    // 3bits reserved
    int16_t elementary_PID; //13bits
    
    // 2B
    // 4bits reserved
    int16_t ES_info_length; //12bits
    
    char* ES_info; //[ES_info_length] bytes.
    
    TSPMTESInfo();
    virtual ~TSPMTESInfo();
};


/**
* 2.4.4.8 Program Map Table. page 64.
*/
class TSPayloadPMT
{
public:
    // 1B
    u_int8_t table_id; //8bits
    
    // 2B
    int8_t section_syntax_indicator; //1bit
    int8_t const0_value; //1bit
    // 2bits reserved. 
    u_int16_t section_length; //12bits
    
    // 2B
    u_int16_t program_number; //16bits
    
    // 1B
    // 2bits reerverd.
    int8_t version_number; //5bits
    int8_t current_next_indicator; //1bit
    
    // 1B
    u_int8_t section_number; //8bits
    
    // 1B
    u_int8_t last_section_number; //8bits
    
    // 2B
    // 2bits reserved.
    int16_t PCR_PID; //16bits
    
    // 2B
    // 4bits reserved.
    int16_t program_info_length; //12bits
    char* program_info_desc; //[program_info_length]bytes
    
    // array of TSPMTESInfo.
    std::vector<TSPMTESInfo*> ES_info; 
    
    // 4B
    int32_t CRC_32; //32bits
    
    TSPayloadPMT();
    virtual ~TSPayloadPMT();
    TSPMTESInfo* at(int index);
    int demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg);
};

/**
* Table 2-18 â€“ Stream_id assignments. page 52.
*/
enum TSPESStreamId
{
	PES_program_stream_map 		= 0b10111100, // 0xbc
	PES_private_stream_1 		= 0b10111101, // 0xbd
	PES_padding_stream			= 0b10111110, // 0xbe
	PES_private_stream_2		= 0b10111111, // 0xbf
	
	// 110x xxxx
	// ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7 or ISO/IEC
	// 14496-3 audio stream number x xxxx
	// (stream_id>>5)&0x07 == PES_audio_prefix
	PES_audio_prefix			= 0b110,
	
	// 1110 xxxx
	// ITU-T Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 11172-2 or ISO/IEC
	// 14496-2 video stream number xxxx
	// (stream_id>>4)&0x0f == PES_audio_prefix
	PES_video_prefix			= 0b1110,
	
	PES_ECM_stream				= 0b11110000, // 0xf0
	PES_EMM_stream				= 0b11110001, // 0xf1
	PES_DSMCC_stream			= 0b11110010, // 0xf2
	PES_13522_stream			= 0b11110011, // 0xf3
	PES_H_222_1_type_A			= 0b11110100, // 0xf4
	PES_H_222_1_type_B			= 0b11110101, // 0xf5
	PES_H_222_1_type_C			= 0b11110110, // 0xf6
	PES_H_222_1_type_D			= 0b11110111, // 0xf7
	PES_H_222_1_type_E			= 0b11111000, // 0xf8
	PES_ancillary_stream		= 0b11111001, // 0xf9
	PES_SL_packetized_stream	= 0b11111010, // 0xfa
	PES_FlexMux_stream			= 0b11111011, // 0xfb
	// reserved data stream
	// 1111 1100 â€¦ 1111 1110
	PES_program_stream_directory= 0b11111111, // 0xff
};


/**
* 2.4.3.7 Semantic definition of fields in PES packet. page 49.
*/
class TSPayloadPES
{
public:
    // 3B
    int32_t packet_start_code_prefix; //24bits
    // 1B
    u_int8_t stream_id; //8bits
    // 2B
    u_int16_t PES_packet_length; //16bits

	// 1B
	// 2bits const '10'
	int8_t PES_scrambling_control; //2bits
	int8_t PES_priority; //1bit
	int8_t data_alignment_indicator; //1bit
	int8_t copyright; //1bit
	int8_t original_or_copy; //1bit
	
	// 1B
	int8_t PTS_DTS_flags; //2bits
	int8_t ESCR_flag; //1bit
	int8_t ES_rate_flag; //1bit
	int8_t DSM_trick_mode_flag; //1bit
	int8_t additional_copy_info_flag; //1bit
	int8_t PES_CRC_flag; //1bit
	int8_t PES_extension_flag; //1bit
	
	// 1B
	u_int8_t PES_header_data_length; //8bits
	
	int64_t pts; // 33bits
	int64_t dts; // 33bits
	
	int16_t ESCR_extension; //9bits
	int64_t ESCR_base; //33bits
	int32_t ES_rate; //22bits
	
	int8_t trick_mode_control; //3bits
	int8_t trick_mode_value; //5bits
	
	int8_t additional_copy_info; //7bits
	int16_t previous_PES_packet_CRC; //16bits
	
	int8_t PES_private_data_flag; //1bit
	int8_t pack_header_field_flag; //1bit
	int8_t program_packet_sequence_counter_flag; //1bit
	int8_t P_STD_buffer_flag; //1bit
	// reserved 3bits
	int8_t PES_extension_flag_2; //1bit
	
	// 16B
	char* PES_private_data; //128bits
	
	int8_t pack_field_length; //8bits
	char* pack_field; //[pack_field_length] bytes
				
	int8_t program_packet_sequence_counter; //7bits
	int8_t MPEG1_MPEG2_identifier; //1bit
	int8_t original_stuff_length; //6bits
				
	int8_t P_STD_buffer_scale; //1bit
	int16_t P_STD_buffer_size; //13bits
	
	int8_t PES_extension_field_length; //7bits
	char* PES_extension_field; //[PES_extension_field_length] bytes
	
	int stuffing_size;
	char* stuffing_byte;
    
    TSPayloadPES();
    virtual ~TSPayloadPES();
    int64_t decode_33bits_int(u_int8_t*& p, int64_t& temp);
    int64_t decode_33bits_int(int64_t& temp);
    int demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg);
};

class TSPayloadReserved 
{
public:
    int size;
    char* bytes;
    
    TSPayloadReserved();
    virtual ~TSPayloadReserved();
    int demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg);
};

struct TSPid
{
	TSPidType type;
	int16_t pid;
};

/**
* logic audio/video message
*/
class TSMessage
{
public:
	// 2.4.3.2 Transport Stream packet layer. page 36
	// the pid of PES packet.
	int16_t pid;
	
	// the type of pid.
	TSPidType type;
	
	// 2.4.3.7 Semantic definition of fields in PES packet. page 49
	// PES packet header size plus data size.
    u_int16_t PES_packet_length; //16bits

	// the stream id.
	u_int8_t stream_id;
	
	// 2.4.3.7 Semantic definition of fields in PES packet. page 49.
	int32_t packet_start_code_prefix;
    
    // header size.
    int packet_header_size;
    
    // the parsed packet size.
    int parsed_packet_size;
    
    // total packet size.
	int packet_data_size;
	char* packet_data;
	
	TSMessage();
	virtual ~TSMessage();
	
	void append(u_int8_t*& p, int size);
	void detach(TSContext* ctx, TSMessage*& pmsg);
	
	bool is_video();
};

// ts context
class TSContext
{
public:
    /**
    * consumed pids.
    */
    int pid_size;
	TSPid* pids;
	std::map<int16_t, TSMessage*> msgs;
    
    TSContext();
    virtual ~TSContext();
    bool exists(int16_t pid);
    TSPid* get(int16_t pid);
    void push(TSPidType type, int16_t pid);
    
    TSMessage* get_msg(int16_t pid);
    void detach(TSMessage* msg);
};

TSContext::TSContext()
{
    pid_size = 0;
    pids = NULL;
}

TSContext::~TSContext()
{
    srs_freepa(pids);
    
    std::map<int16_t, TSMessage*>::iterator it;
    for (it = msgs.begin(); it != msgs.end(); ++it) {
        TSMessage* msg = it->second;
        srs_freep(msg);
    }
    msgs.clear();
}

bool TSContext::exists(int16_t pid)
{
    for (int i = 0; i < pid_size; i++) {
        if (pid == pids[i].pid) {
            return true;
        }
    }
    
    return false;
}

TSPid* TSContext::get(int16_t pid)
{
    for (int i = 0; i < pid_size; i++) {
        if (pid == pids[i].pid) {
            return &pids[i];
        }
    }
    
    return NULL;
}

void TSContext::push(TSPidType type, int16_t pid)
{
    if (exists(pid)) {
        return;
    }
    
	TSPid* p = new TSPid[pid_size + 1];
	memcpy(p, pids, sizeof(TSPid) * pid_size);
	
	p[pid_size] = (TSPid){type, pid};
	pid_size++;
	
	srs_freepa(pids);
	pids = p;
}

TSMessage* TSContext::get_msg(int16_t pid)
{
	if (msgs[pid] == NULL) {
		TSMessage* msg = new TSMessage();
		msg->pid = pid;
		msgs[pid] = msg;
	}
	
	return msgs[pid];
}

void TSContext::detach(TSMessage* msg)
{
	msgs[msg->pid] = NULL;
}

TSMessage::TSMessage()
{
	pid = 0;
	type = TSPidTypeReserved;
	stream_id = 0;
	packet_start_code_prefix = 0;
	PES_packet_length = 0;
	packet_header_size = 0;
	parsed_packet_size = 0;
	packet_data_size = 0;
	packet_data = NULL;
}

TSMessage::~TSMessage()
{
	srs_freepa(packet_data);
}

void TSMessage::append(u_int8_t*& p, int size)
{
	if (size > 0) {
		memcpy(packet_data + parsed_packet_size, p, size);
		p += size;
		parsed_packet_size += size;
	}
}

void TSMessage::detach(TSContext* ctx, TSMessage*& pmsg)
{
	if (parsed_packet_size >= packet_data_size) {
		ctx->detach(this);
		pmsg = this;
	}
}

bool TSMessage::is_video()
{
	return type == TSPidTypeVideo;
}

TSAdaptionField::TSAdaptionField()
{
    adaption_field_length = 0;
    discontinuity_indicator = 0;
    random_access_indicator = 0;
    elementary_stream_priority_indicator = 0;
    PCR_flag = 0;
    OPCR_flag = 0;
    splicing_point_flag = 0;
    transport_private_data_flag = 0;
    adaptation_field_extension_flag = 0;
    program_clock_reference_base = 0;
    program_clock_reference_extension = 0;
    original_program_clock_reference_base = 0;
    original_program_clock_reference_extension = 0;
    splice_countdown = 0;
    transport_private_data_length = 0;
    transport_private_data = NULL;
    adaptation_field_extension_length = 0;
    ltw_flag = 0;
    piecewise_rate_flag = 0;
    seamless_splice_flag = 0;
    ltw_valid_flag = 0;
    ltw_offset = 0;
    piecewise_rate = 0;
    splice_type = 0;
    DTS_next_AU0 = 0;
    marker_bit0 = 0;
    DTS_next_AU1 = 0;
    marker_bit1 = 0;
    DTS_next_AU2 = 0;
    marker_bit2 = 0;
    af_ext_reserved = NULL;
    af_reserved = NULL;
    __user_size = 0;
}

TSAdaptionField::~TSAdaptionField()
{
    srs_freepa(transport_private_data);
    srs_freepa(af_ext_reserved);
    srs_freepa(af_reserved);
}

int TSAdaptionField::get_size()
{
    return __user_size;
}

int TSAdaptionField::demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg)
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

TSPayloadReserved::TSPayloadReserved()
{
    size = 0;
    bytes = NULL;
}

TSPayloadReserved::~TSPayloadReserved()
{
    srs_freepa(bytes);
}

int TSPayloadReserved::demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg)
{
    int ret = 0;
    
    size = pkt->payload->size - pkt->payload->pointer_field_size;

    // not parsed bytes.
    if (size > 0) {
        bytes = new char[size];
        memcpy(bytes, p, size);
        p += size;
    }

    return ret;
}

TSPayloadPAT::TSPayloadPAT()
{
    table_id = 0;
    section_syntax_indicator = 0;
    const0_value = 0;
    section_length = 0;
    transport_stream_id = 0;
    version_number = 0;
    current_next_indicator = 0;
    section_number = 0;
    last_section_number = 0;
    program_size = 0;
    programs = NULL;
    CRC_32 = 0;
}

TSPayloadPAT::~TSPayloadPAT()
{
    srs_freepa(programs);
}

int TSPayloadPAT::demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg)
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
            
            int16_t pid = programs[i] & 0x1FFF;
            ctx->push(TSPidTypePMT, pid);
        }
    }
    
    pp = (char*)&CRC_32;
    pp[3] = *p++;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return ret;
}

TSPMTESInfo::TSPMTESInfo()
{
    stream_type = 0;
    elementary_PID = 0;
    ES_info_length = 0;
	ES_info = NULL;
}

TSPMTESInfo::~TSPMTESInfo()
{
	srs_freepa(ES_info);
}

TSPayloadPMT::TSPayloadPMT()
{
    table_id = 0;
    section_syntax_indicator = 0;
    const0_value = 0;
    section_length = 0;
    program_number = 0;
    version_number = 0;
    current_next_indicator = 0;
    section_number = 0;
    last_section_number = 0;
    PCR_PID = 0;
    program_info_length = 0;
    program_info_desc = NULL;
    CRC_32 = 0;
}

TSPayloadPMT::~TSPayloadPMT()
{
	srs_freepa(program_info_desc);
	
	for (std::vector<TSPMTESInfo*>::iterator it = ES_info.begin(); it != ES_info.end(); ++it) {
		TSPMTESInfo* info = *it;
		srs_freep(info);
	}
	ES_info.clear();
}

TSPMTESInfo* TSPayloadPMT::at(int index)
{
	return ES_info.at(index);
}

int TSPayloadPMT::demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg)
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
    
    pp = (char*)&program_number;
    pp[1] = *p++;
    pp[0] = *p++;
    
    current_next_indicator = *p++;
    version_number = (current_next_indicator >> 1) & 0x1F;
    current_next_indicator &= 0x01;
    
    section_number = *p++;
    last_section_number = *p++;
    
    pp = (char*)&PCR_PID;
    pp[1] = *p++;
    pp[0] = *p++;
    
    PCR_PID &= 0x1FFF;
    
    pp = (char*)&program_info_length;
    pp[1] = *p++;
    pp[0] = *p++;
    
    program_info_length &= 0xFFF;
    
    if (program_info_length > 0) {
	    program_info_desc = new char[program_info_length];
	    memcpy(program_info_desc, p, program_info_length);
	    p += program_info_length;
    }
    
    // [section_length] - 4(CRC) - 9B - [program_info_length]
    int ES_bytes = section_length - 4 - 9 - program_info_length;
    while (ES_bytes > 0) {
	    TSPMTESInfo* info = new TSPMTESInfo();
	    
	    info->stream_type = *p++;
	    ES_bytes--;
	    
	    pp = (char*)&info->elementary_PID;
	    pp[1] = *p++;
	    pp[0] = *p++;
	    ES_bytes -= 2;
	    
	    info->elementary_PID &= 0x1FFF;
	    
	    pp = (char*)&info->ES_info_length;
	    pp[1] = *p++;
	    pp[0] = *p++;
	    ES_bytes -= 2;
	    
	    info->ES_info_length &= 0x0FFF;
	    
	    if (info->ES_info_length > 0) {
	        info->ES_info = new char[info->ES_info_length];
	        memcpy(info->ES_info, p, info->ES_info_length);
	        
	        p += info->ES_info_length;
	        ES_bytes -= info->ES_info_length;
	    }
	    
	    ES_info.push_back(info);
		
		// TODO: support more video type.
		if (info->stream_type == TSStreamTypeVideoH264) {
			ctx->push(TSPidTypeVideo, info->elementary_PID);
		}
		// TODO: support more audio type.
		if (info->stream_type == TSStreamTypeAudioAAC) {
			ctx->push(TSPidTypeAudio, info->elementary_PID);
		}
    }
    
    pp = (char*)&CRC_32;
    pp[3] = *p++;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return ret;
}

TSPayloadPES::TSPayloadPES()
{
    packet_start_code_prefix = 0;
    stream_id = 0;
    PES_packet_length = 0;
    PES_scrambling_control = 0;
    PES_priority = 0;
    data_alignment_indicator = 0;
    copyright = 0;
    original_or_copy = 0;
    PTS_DTS_flags = 0;
    ESCR_flag = 0;
    ES_rate_flag = 0;
    DSM_trick_mode_flag = 0;
    additional_copy_info_flag = 0;
    PES_CRC_flag = 0;
    PES_extension_flag = 0;
    PES_header_data_length = 0;
    pts = 0;
    dts = 0;
    ESCR_extension = 0;
    ESCR_base = 0;
    ES_rate = 0;
    trick_mode_control = 0;
    trick_mode_value = 0;
    additional_copy_info = 0;
    previous_PES_packet_CRC = 0;
    PES_private_data_flag = 0;
    pack_header_field_flag = 0;
    program_packet_sequence_counter_flag = 0;
    P_STD_buffer_flag = 0;
    PES_extension_flag_2 = 0;
    PES_private_data = NULL;
    pack_field_length = 0;
    pack_field = NULL;
    program_packet_sequence_counter = 0;
    MPEG1_MPEG2_identifier = 0;
    original_stuff_length = 0;
    P_STD_buffer_scale = 0;
    P_STD_buffer_size = 0;
    PES_extension_field_length = 0;
    PES_extension_field = NULL;
    stuffing_size = 0;
    stuffing_byte = NULL;
}

TSPayloadPES::~TSPayloadPES()
{
	srs_freepa(PES_private_data);
	srs_freepa(pack_field);
	srs_freepa(PES_extension_field);
	srs_freepa(stuffing_byte);
}

int64_t TSPayloadPES::decode_33bits_int(u_int8_t*& p, int64_t& temp)
{
	char* pp = (char*)&temp;
	pp[4] = *p++;
	pp[3] = *p++;
	pp[2] = *p++;
	pp[1] = *p++;
	pp[0] = *p++;
	
	return decode_33bits_int(temp);
}

int64_t TSPayloadPES::decode_33bits_int(int64_t& temp)
{
    int64_t ret = 0;
	
	// marker_bit 1bit
	temp = temp >> 1;
	// PTS [14..0] 15bits
	ret |= temp & 0x3fff;
	// marker_bit 1bit
	temp = temp >> 1;
	// PTS [29..15] 15bits, 15zero, 29-15+1one
	ret |= temp & 0x3fff8000;
	// marker_bit 1bit
	temp = temp >> 1;
	// PTS [32..30] 3bits
	ret |= temp & 0x1c0000000;
	
	temp = temp >> 33;
	
	return ret;
}

int TSPayloadPES::demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg)
{
    int ret = 0;
    
    if (!pkt->header->payload_unit_start_indicator) {
        TSMessage* msg = ctx->get_msg(pkt->header->pid);
		if (msg->packet_start_code_prefix != 0x01) {
			trace("ts+pes decode continous packet error, msg is empty.");
			return -1;
		}
		msg->append(p, last - p);
		msg->detach(ctx, pmsg);
		return ret;
    }
    
    char* pp = (char*)&packet_start_code_prefix;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    packet_start_code_prefix &= 0xFFFFFF;

	stream_id = *p++;
	
	pp = (char*)&PES_packet_length;
	pp[1] = *p++;
	pp[0] = *p++;
	u_int8_t* pos_packet = p;
	
	if (stream_id != PES_program_stream_map
	    && stream_id != PES_padding_stream
	    && stream_id != PES_private_stream_2
	    && stream_id != PES_ECM_stream
	    && stream_id != PES_EMM_stream
	    && stream_id != PES_program_stream_directory
	    && stream_id != PES_DSMCC_stream
	    && stream_id != PES_H_222_1_type_E
	) {
		original_or_copy = *p++;
		
		//int8_t const2bits = (original_or_copy >> 6) & 0x03;
		PES_scrambling_control = (original_or_copy >> 4) & 0x03;
		PES_priority = (original_or_copy >> 3) & 0x01;
		data_alignment_indicator = (original_or_copy >> 2) & 0x01;
		copyright = (original_or_copy >> 1) & 0x01;
		original_or_copy &= 0x01;
		
		PES_extension_flag = *p++;
		
		PTS_DTS_flags = (PES_extension_flag >> 6) & 0x03;
		ESCR_flag = (PES_extension_flag >> 5) & 0x01;
		ES_rate_flag = (PES_extension_flag >> 4) & 0x01;
		DSM_trick_mode_flag = (PES_extension_flag >> 3) & 0x01;
		additional_copy_info_flag = (PES_extension_flag >> 2) & 0x01;
		PES_CRC_flag = (PES_extension_flag >> 1) & 0x01;
		PES_extension_flag &= 0x01;
		
		PES_header_data_length = *p++;
		u_int8_t* pos_header = p;

		int64_t temp = 0;
		if (PTS_DTS_flags == 0x2) {
			pts = decode_33bits_int(p, temp);
			// '0010' 4bits
			//int8_t const4bits = temp & 0x0F;
		}

		if (PTS_DTS_flags == 0x3) {
			pts = decode_33bits_int(p, temp);
			// '0011' 4bits
			//int8_t const4bits = temp & 0x0F;
			
			dts = decode_33bits_int(p, temp);
			// '0001' 4bits
			//int8_t const4bits = temp & 0x0F;
		}
		
		if (ESCR_flag) {
			pp = (char*)&temp;
			pp[5] = *p++;
			pp[4] = *p++;
			pp[3] = *p++;
			pp[2] = *p++;
			pp[1] = *p++;
			pp[0] = *p++;
			
			// marker_bit 1bit
			temp = temp >> 1;
			// ESCR_extension 9bits
			ESCR_extension = temp & 0x1f;
			temp = temp >> 9;
			
			ESCR_base = decode_33bits_int(temp);
			
			// reserved 2bits
			//int8_t reserved2bits = temp & 0x03;
		}
		
		if (ES_rate_flag) {
			pp = (char*)&ES_rate;
			pp[2] = *p++;
			pp[1] = *p++;
			pp[0] = *p++;
			
			ES_rate = ES_rate >> 1;
			ES_rate &= 0x3FFFFF;
		}

		if (DSM_trick_mode_flag) {
			trick_mode_control = *p++;

			trick_mode_value = trick_mode_control & 0x1f;
			trick_mode_control = (trick_mode_control >> 5) & 0x03;
		}
		
		if (additional_copy_info_flag) {
			additional_copy_info = *p++;
			additional_copy_info &= 0x7f;
		}
		
		if (PES_CRC_flag) {
			pp = (char*)&previous_PES_packet_CRC;
			pp[1] = *p++;
			pp[0] = *p++;
		}

		if (PES_extension_flag) {
			PES_extension_flag_2 = *p++;
			
			PES_private_data_flag = (PES_extension_flag_2 >> 7) & 0x01;
			pack_header_field_flag = (PES_extension_flag_2 >> 6) & 0x01;
			program_packet_sequence_counter_flag = (PES_extension_flag_2 >> 5) & 0x01;
			P_STD_buffer_flag = (PES_extension_flag_2 >> 4) & 0x01;
			PES_extension_flag_2 &= PES_extension_flag_2 & 0x01;
			
			if (PES_private_data_flag) {
				PES_private_data = new char[16];
				memcpy(PES_private_data, p, 16);
				p += 16;
			}
			
			if (pack_header_field_flag) {
				pack_field_length = *p++;
				if (pack_field_length > 0) {
					pack_field = new char[pack_field_length];
					memcpy(pack_field, p, pack_field_length);
					p += pack_field_length;
				}
			}
			
			if (program_packet_sequence_counter_flag) {
				program_packet_sequence_counter = *p++;
				program_packet_sequence_counter &= 0x7f;
				
				original_stuff_length = *p++;
				MPEG1_MPEG2_identifier = (original_stuff_length >> 6) & 0x01;
				original_stuff_length &= 0x3f;
			}
			
			if (P_STD_buffer_flag) {
				pp = (char*)&P_STD_buffer_size;
				pp[1] = *p++;
				pp[0] = *p++;
				
				// '01'
				//int8_t const2bits = (P_STD_buffer_scale >>14) & 0x03;

				P_STD_buffer_scale = (P_STD_buffer_scale >>13) & 0x01;
				P_STD_buffer_size &= 0x1FFF;
			}
			
			if (PES_extension_flag_2) {
				PES_extension_field_length = *p++;
				PES_extension_field_length &= 0x07;
				
				if (PES_extension_field_length > 0) {
					PES_extension_field = new char[PES_extension_field_length];
					memcpy(PES_extension_field, p, PES_extension_field_length);
					p += PES_extension_field_length;
				}
			}
		}
		
		// stuffing_byte
		int stuffing_size = PES_header_data_length - (p - pos_header);
		if (stuffing_size > 0) {
			stuffing_byte = new char[stuffing_size];
			memcpy(stuffing_byte, p, stuffing_size);
			p += stuffing_size;
		}
		
		// get the pid.
		TSPid* pid = ctx->get(pkt->header->pid);
		if (!pid) {
			trace("ts+pes pid: %d type is invalid.", pkt->header->pid);
		}
		
		// get the message to build from the chunks(PES packets).
		TSMessage* msg = ctx->get_msg(pid->pid);

		msg->type = pid->type;
		msg->stream_id = stream_id;
		msg->packet_start_code_prefix = packet_start_code_prefix;
		
		// PES_packet_data_byte, page58.
		// the packet size contains the header size.
		// The number of PES_packet_data_bytes, N, is specified by the 
		// PES_packet_length field. N shall be equal to the value 
		// indicated in the PES_packet_length minus the number of bytes 
		// between the last byte of the PES_packet_length field and the 
		// first PES_packet_data_byte.
		msg->PES_packet_length = PES_packet_length;
		msg->packet_header_size = p - pos_packet;
		msg->packet_data_size = PES_packet_length - msg->packet_header_size;
		
		if (msg->packet_data_size > 0) {
			msg->packet_data = new char[msg->packet_data_size];
		}
		
		// PES_packet_data_byte
		int size = srs_min(msg->packet_data_size, last - p);
		msg->append(p, size);
		
		msg->detach(ctx, pmsg);
	
		trace("ts+pes stream_id: %d size: %d pts: %"PRId64" dts: %"PRId64" packet_size: %d parsed_size: %d",
			stream_id, PES_packet_length, pts, dts, msg->packet_data_size, msg->parsed_packet_size);
	} else if (stream_id == PES_program_stream_map
		|| stream_id == PES_private_stream_2
		|| stream_id == PES_ECM_stream
		|| stream_id == PES_EMM_stream
		|| stream_id == PES_program_stream_directory
		|| stream_id == PES_DSMCC_stream
		|| stream_id == PES_H_222_1_type_E
	) {
		// for (i = 0; i < PES_packet_length; i++) {
		// 		PES_packet_data_byte
		// }
	} else if (stream_id != PES_padding_stream) {
		// for (i = 0; i < PES_packet_length; i++) {
		// 		padding_byte
		// }
	}
    
    return ret;
}

/**
* 2.4.3.6 PES packet. page 49.
*/

TSPayload::TSPayload()
{
    size = 0;
    pointer_field_size = 0;
    type = TSPidTypeReserved;
    pointer_field = 0;
    reserved = NULL;
    pat = NULL;
    pmt = NULL;
    pes = NULL;
    
}

TSPayload::~TSPayload()
{
    srs_freep(reserved);
    srs_freep(pat);
    srs_freep(pmt);
    srs_freep(pes);
}

void TSPayload::read_pointer_field(TSPacket* pkt, u_int8_t*& p)
{
    if (pkt->header->payload_unit_start_indicator) {
        pointer_field = *p++;
        pointer_field_size = 1;
    }
}

int TSPayload::demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg)
{
    int ret = 0;
    
    if (pkt->header->pid == PID_PAT) {
        read_pointer_field(pkt, p);
        
        type = TSPidTypePAT;
        pat = new TSPayloadPAT();
        return pat->demux(ctx, pkt, start, last, p, pmsg);
    }
    
    TSPid* pid = ctx->get(pkt->header->pid);
    if (pid && pid->type == TSPidTypePMT) {
        read_pointer_field(pkt, p);
        
        type = pid->type;
        pmt = new TSPayloadPMT();
        return pmt->demux(ctx, pkt, start, last, p, pmsg);
    }
    if (pid && (pid->type == TSPidTypeVideo || pid->type == TSPidTypeAudio)) {
        type = pid->type;
        pes = new TSPayloadPES();
        return pes->demux(ctx, pkt, start, last, p, pmsg);
    }
    
    // not parsed bytes.
    type = TSPidTypeReserved;
    reserved = new TSPayloadReserved();
    if ((ret = reserved->demux(ctx, pkt, start, last, p, pmsg)) != 0) {
        return ret;
    }
    
    return ret;
}

TSPacket::TSPacket()
{
    header = new TSHeader();
    adaption_field = new TSAdaptionField();
    payload = new TSPayload();
}

TSPacket::~TSPacket()
{
    srs_freep(header);
    srs_freep(adaption_field);
    srs_freep(payload);
}

int TSPacket::demux(TSContext* ctx, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg)
{
    int ret = 0;
    
    if ((ret = header->demux(ctx, this, start, last, p, pmsg))  != 0) {
        return ret;
    }

    if (header->adaption_field_control == AFC_ADAPTION_ONLY || header->adaption_field_control == AFC_BOTH) {
        if ((ret = adaption_field->demux(ctx, this, start, last, p, pmsg)) != 0) {
            trace("ts+header af(adaption field) decode error. ret=%d", ret);
            return ret;
        }
        trace("ts+header af(adaption field) decoded.");
    }
    
    // calc the user defined data size for payload.
    payload->size = TS_PACKET_SIZE - header->get_size() - adaption_field->get_size();
    
    if (header->adaption_field_control == AFC_PAYLOAD_ONLY || header->adaption_field_control == AFC_BOTH) {
        if ((ret = payload->demux(ctx, this, start, last, p, pmsg)) != 0) {
            trace("ts+header payload decode error. ret=%d", ret);
            return ret;
        }
        trace("ts+header payload decoded.");
    }
    
    trace("ts+header parsed finished. parsed: %d left: %d header: %d payload: %d(%d+%d)", 
        (int)(p - start), (int)(last - p), header->get_size(), payload->size, payload->pointer_field_size, 
        payload->size - payload->pointer_field_size);
    
    return finish();
}

int TSPacket::finish()
{
    return 0;
}
    
TSHeader::TSHeader()
{
    sync_byte = 0;
    transport_error_indicator = 0;
    payload_unit_start_indicator = 0;
    transport_priority = 0;
    pid = 0;
    transport_scrambling_control = 0;
    adaption_field_control = 0;
    continuity_counter = 0;
}

TSHeader::~TSHeader()
{
}

int TSHeader::get_size()
{
    return 4;
}

int TSHeader::demux(TSContext* ctx, TSPacket* pkt, u_int8_t* start, u_int8_t* last, u_int8_t*& p, TSMessage*& pmsg)
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
    
    ctx->push(TSPidTypePAT, pid);
    
    continuity_counter = *p++;
    
    transport_scrambling_control = (continuity_counter >> 6) & 0x03;
    adaption_field_control = (continuity_counter >> 4) & 0x03;
    continuity_counter &= 0x0F;
    
    trace("ts+header sync: %#x error: %d unit_start: %d priotiry: %d pid: %d scrambling: %d adaption: %d counter: %d",
        sync_byte, transport_error_indicator, payload_unit_start_indicator, transport_priority, pid,
        transport_scrambling_control, adaption_field_control, continuity_counter);
        
    return ret;
}

int main(int /*argc*/, char** /*argv*/)
{
    const char* file = "livestream-1347.ts";
    //file = "nginx-rtmp-hls/livestream-1347-currupt.ts";
    int fd = open(file, O_RDONLY);
    
    int ret = 0;
    trace("demuxer+read packet count offset T+0  T+1  T+2  T+3  T+x T+L2 T+L1 T+L0");
    
    TSContext ctx;
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
        TSMessage* msg = NULL;
        if ((ret = pkt.demux(&ctx, start, last, p, msg)) != 0) {
            trace("demuxer+read decode ts packet error. ret=%d", ret);
            return ret;
        }
        
        // TODO: process it.
        srs_freep(msg);
        
        offset += nread;
    }
    
    close(fd);
    return ret;
}

