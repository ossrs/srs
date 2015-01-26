/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_KERNEL_TS_HPP
#define SRS_KERNEL_TS_HPP

/*
#include <srs_kernel_ts.hpp>
*/
#include <srs_core.hpp>

#include <string>

#include <srs_kernel_codec.hpp>

class SrsTsCache;
class SrsTSMuxer;
class SrsFileWriter;
class SrsFileReader;
class SrsAvcAacCodec;
class SrsCodecSample;
class SrsSimpleBuffer;

// Transport Stream packets are 188 bytes in length.
#define SRS_TS_PACKET_SIZE          188

// @see: ngx_rtmp_SrsMpegtsFrame_t
class SrsMpegtsFrame
{
public:
    int64_t         pts;
    int64_t         dts;
    int             pid;
    int             sid;
    int             cc;
    bool            key;
    
    SrsMpegtsFrame();
};

/**
* the pid of ts packet,
* Table 2-3 每 PID table, hls-mpeg-ts-iso13818-1.pdf, page 37
*/
enum SrsTsPid
{
    // Program Association Table(see Table 2-25).
    SrsTsPidPAT    = 0x00,
    // Conditional Access Table (see Table 2-27).
    SrsTsPidCAT    = 0x01,
    // Transport Stream Description Table
    SrsTsPidTSDT    = 0x02,
    // null packets (see Table 2-3)
    SrsTsPidNULL    = 0x01FFF,
};

/**
* the transport_scrambling_control of ts packet,
* Table 2-4 每 Scrambling control values, hls-mpeg-ts-iso13818-1.pdf, page 38
*/
enum SrsTsScrambled
{
    // Not scrambled
    SrsTsScrambledDisabled      = 0x00,
    // User-defined
    SrsTsScrambledUserDefined1  = 0x01,
    // User-defined
    SrsTsScrambledUserDefined2  = 0x02,
    // User-defined
    SrsTsScrambledUserDefined3  = 0x03,
};

/**
* the adaption_field_control of ts packet,
* Table 2-5 每 Adaptation field control values, hls-mpeg-ts-iso13818-1.pdf, page 38
*/
enum SrsTsAdaptationFieldType
{
    // Reserved for future use by ISO/IEC
    SrsTsAdaptationFieldTypeReserved      = 0x00,
    // No adaptation_field, payload only
    SrsTsAdaptationFieldTypePayloadOnly   = 0x01,
    // Adaptation_field only, no payload
    SrsTsAdaptationFieldTypeAdaptionOnly  = 0x02,
    // Adaptation_field followed by payload
    SrsTsAdaptationFieldTypeBoth          = 0x03,
};

class SrsTsAdaptationField
{
};

/**
* the packet in ts stream,
* 2.4.3.2 Transport Stream packet layer, hls-mpeg-ts-iso13818-1.pdf, page 36
* Transport Stream packets shall be 188 bytes long.
*/
class SrsTsPacket
{
private:
    /**
    * The sync_byte is a fixed 8-bit field whose value is '0100 0111' (0x47). Sync_byte emulation in the choice of
    * values for other regularly occurring fields, such as PID, should be avoided.
    */
    int8_t sync_byte; //8bits
    /**
    * The transport_error_indicator is a 1-bit flag. When set to '1' it indicates that at least
    * 1 uncorrectable bit error exists in the associated Transport Stream packet. This bit may be set to '1' by entities external to
    * the transport layer. When set to '1' this bit shall not be reset to '0' unless the bit value(s) in error have been corrected.
    */
    int8_t transport_error_indicator; //1bit
    /**
    * The payload_unit_start_indicator is a 1-bit flag which has normative meaning for
    * Transport Stream packets that carry PES packets (refer to 2.4.3.6) or PSI data (refer to 2.4.4).
    * 
    * When the payload of the Transport Stream packet contains PES packet data, the payload_unit_start_indicator has the
    * following significance: a '1' indicates that the payload of this Transport Stream packet will commence with the first byte
    * of a PES packet and a '0' indicates no PES packet shall start in this Transport Stream packet. If the
    * payload_unit_start_indicator is set to '1', then one and only one PES packet starts in this Transport Stream packet. This
    * also applies to private streams of stream_type 6 (refer to Table 2-29).
    *
    * When the payload of the Transport Stream packet contains PSI data, the payload_unit_start_indicator has the following
    * significance: if the Transport Stream packet carries the first byte of a PSI section, the payload_unit_start_indicator value
    * shall be '1', indicating that the first byte of the payload of this Transport Stream packet carries the pointer_field. If the
    * Transport Stream packet does not carry the first byte of a PSI section, the payload_unit_start_indicator value shall be '0',
    * indicating that there is no pointer_field in the payload. Refer to 2.4.4.1 and 2.4.4.2. This also applies to private streams of
    * stream_type 5 (refer to Table 2-29).
    * 
    * For null packets the payload_unit_start_indicator shall be set to '0'.
    * 
    * The meaning of this bit for Transport Stream packets carrying only private data is not defined in this Specification.
    */
    int8_t payload_unit_start_indicator; //1bit
    /**
    * The transport_priority is a 1-bit indicator. When set to '1' it indicates that the associated packet is
    * of greater priority than other packets having the same PID which do not have the bit set to '1'. The transport mechanism
    * can use this to prioritize its data within an elementary stream. Depending on the application the transport_priority field
    * may be coded regardless of the PID or within one PID only. This field may be changed by channel specific encoders or
    * decoders.
    */
    int8_t transport_priority; //1bit
    /**
    * The PID is a 13-bit field, indicating the type of the data stored in the packet payload. PID value 0x0000 is
    * reserved for the Program Association Table (see Table 2-25). PID value 0x0001 is reserved for the Conditional Access
    * Table (see Table 2-27). PID values 0x0002 每 0x000F are reserved. PID value 0x1FFF is reserved for null packets (see
    * Table 2-3).
    */
    SrsTsPid pid; //13bits
    /**
    * This 2-bit field indicates the scrambling mode of the Transport Stream packet payload.
    * The Transport Stream packet header, and the adaptation field when present, shall not be scrambled. In the case of a null
    * packet the value of the transport_scrambling_control field shall be set to '00' (see Table 2-4).
    */
    SrsTsScrambled transport_scrambling_control; //2bits
    /**
    * This 2-bit field indicates whether this Transport Stream packet header is followed by an
    * adaptation field and/or payload (see Table 2-5).
    *
    * ITU-T Rec. H.222.0 | ISO/IEC 13818-1 decoders shall discard Transport Stream packets with the
    * adaptation_field_control field set to a value of '00'. In the case of a null packet the value of the adaptation_field_control
    * shall be set to '01'.
    */
    SrsTsAdaptationFieldType adaption_field_control; //2bits
    /**
    * The continuity_counter is a 4-bit field incrementing with each Transport Stream packet with the
    * same PID. The continuity_counter wraps around to 0 after its maximum value. The continuity_counter shall not be
    *incremented when the adaptation_field_control of the packet equals '00' or '10'.
    * 
    * In Transport Streams, duplicate packets may be sent as two, and only two, consecutive Transport Stream packets of the
    * same PID. The duplicate packets shall have the same continuity_counter value as the original packet and the
    * adaptation_field_control field shall be equal to '01' or '11'. In duplicate packets each byte of the original packet shall be
    * duplicated, with the exception that in the program clock reference fields, if present, a valid value shall be encoded.
    *
    * The continuity_counter in a particular Transport Stream packet is continuous when it differs by a positive value of one
    * from the continuity_counter value in the previous Transport Stream packet of the same PID, or when either of the nonincrementing
    * conditions (adaptation_field_control set to '00' or '10', or duplicate packets as described above) are met.
    * The continuity counter may be discontinuous when the discontinuity_indicator is set to '1' (refer to 2.4.3.4). In the case of
    * a null packet the value of the continuity_counter is undefined.
    */
    u_int8_t continuity_counter; //4bits
private:
    SrsTsAdaptationField* adaptation_field;
public:
    SrsTsPacket();
    virtual ~SrsTsPacket();
public:
};

/**
* write data from frame(header info) and buffer(data) to ts file.
* it's a simple object wrapper for utility from nginx-rtmp: SrsMpegtsWriter
*/
class SrsTSMuxer
{
private:
    SrsCodecAudio previous;
    SrsCodecAudio current;
private:
    SrsFileWriter* writer;
    std::string path;
public:
    SrsTSMuxer(SrsFileWriter* w);
    virtual ~SrsTSMuxer();
public:
    /**
    * open the writer, donot write the PSI of ts.
    */
    virtual int open(std::string _path);
    /**
    * when open ts, we donot write the header(PSI),
    * for user may need to update the acodec to mp3 or others,
    * so we use delay write PSI, when write audio or video.
    * @remark for audio aac codec, for example, SRS1, it's ok to write PSI when open ts.
    * @see https://github.com/winlinvip/simple-rtmp-server/issues/301
    */
    virtual int update_acodec(SrsCodecAudio ac);
    /**
    * write an audio frame to ts, 
    * @remark write PSI first when not write yet.
    */
    virtual int write_audio(SrsMpegtsFrame* af, SrsSimpleBuffer* ab);
    /**
    * write a video frame to ts, 
    * @remark write PSI first when not write yet.
    */
    virtual int write_video(SrsMpegtsFrame* vf, SrsSimpleBuffer* vb);
    /**
    * close the writer.
    */
    virtual void close();
};

/**
* jitter correct for audio,
* the sample rate 44100/32000 will lost precise,
* when mp4/ts(tbn=90000) covert to flv/rtmp(1000),
* so the Hls on ipad or iphone will corrupt,
* @see nginx-rtmp: est_pts
*/
class SrsTsAacJitter
{
private:
    int64_t base_pts;
    int64_t nb_samples;
    int sync_ms;
public:
    SrsTsAacJitter();
    virtual ~SrsTsAacJitter();
    /**
    * when buffer start, calc the "correct" pts for ts,
    * @param flv_pts, the flv pts calc from flv header timestamp,
    * @param sample_rate, the sample rate in format(flv/RTMP packet header).
    * @param aac_sample_rate, the sample rate in codec(sequence header).
    * @return the calc correct pts.
    */
    virtual int64_t on_buffer_start(int64_t flv_pts, int sample_rate, int aac_sample_rate);
    /**
    * when buffer continue, muxer donot write to file,
    * the audio buffer continue grow and donot need a pts,
    * for the ts audio PES packet only has one pts at the first time.
    */
    virtual void on_buffer_continue();
};

/**
* ts stream cache, 
* use to cache ts stream.
* 
* about the flv tbn problem:
*   flv tbn is 1/1000, ts tbn is 1/90000,
*   when timestamp convert to flv tbn, it will loose precise,
*   so we must gather audio frame together, and recalc the timestamp @see SrsTsAacJitter,
*   we use a aac jitter to correct the audio pts.
*/
class SrsTsCache
{
public:
    // current frame and buffer
    SrsMpegtsFrame* af;
    SrsSimpleBuffer* ab;
    SrsMpegtsFrame* vf;
    SrsSimpleBuffer* vb;
public:
    // the audio cache buffer start pts, to flush audio if full.
    // @remark the pts is not the adjust one, it's the orignal pts.
    int64_t audio_buffer_start_pts;
protected:
    // time jitter for aac
    SrsTsAacJitter* aac_jitter;
public:
    SrsTsCache();
    virtual ~SrsTsCache();
public:
    /**
    * write audio to cache
    */
    virtual int cache_audio(SrsAvcAacCodec* codec, int64_t pts, SrsCodecSample* sample);
    /**
    * write video to muxer.
    */
    virtual int cache_video(SrsAvcAacCodec* codec, int64_t dts, SrsCodecSample* sample);
private:
    virtual int do_cache_audio(SrsAvcAacCodec* codec, SrsCodecSample* sample);
    virtual int do_cache_video(SrsAvcAacCodec* codec, SrsCodecSample* sample);
};

/**
* encode data to ts file.
*/
class SrsTsEncoder
{
private:
    SrsFileWriter* _fs;
private:
    SrsAvcAacCodec* codec;
    SrsCodecSample* sample;
    SrsTsCache* cache;
    SrsTSMuxer* muxer;
public:
    SrsTsEncoder();
    virtual ~SrsTsEncoder();
public:
    /**
    * initialize the underlayer file stream.
    */
    virtual int initialize(SrsFileWriter* fs);
public:
    /**
    * write audio/video packet.
    * @remark assert data is not NULL.
    */
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
private:
    virtual int flush_audio();
    virtual int flush_video();
};

#endif

