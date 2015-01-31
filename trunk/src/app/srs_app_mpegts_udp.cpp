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

#include <srs_app_mpegts_udp.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_file.hpp>
#include <srs_core_autofree.hpp>

#ifdef SRS_AUTO_STREAM_CASTER

ISrsUdpHandler::ISrsUdpHandler()
{
}

ISrsUdpHandler::~ISrsUdpHandler()
{
}

SrsMpegtsOverUdp::SrsMpegtsOverUdp(SrsConfDirective* c)
{
    stream = new SrsStream();
    context = new SrsTsContext();
    buffer = new SrsSimpleBuffer();
    output = _srs_config->get_stream_caster_output(c);
}

SrsMpegtsOverUdp::~SrsMpegtsOverUdp()
{
    srs_freep(buffer);
    srs_freep(stream);
    srs_freep(context);
}

int SrsMpegtsOverUdp::on_udp_packet(sockaddr_in* from, char* buf, int nb_buf)
{
    int ret = ERROR_SUCCESS;

    std::string peer_ip = inet_ntoa(from->sin_addr);
    int peer_port = ntohs(from->sin_port);

    // append to buffer.
    buffer->append(buf, nb_buf);

    srs_info("udp: got %s:%d packet %d/%d bytes",
        peer_ip.c_str(), peer_port, nb_buf, buffer->length());

    // collect nMB data to parse in a time.
    // TODO: FIXME: comment the following for release.
    //if (buffer->length() < 3 * 1024 * 1024) return ret;
    // TODO: FIXME: remove the debug to file.
#if 0
    SrsFileWriter fw;
    if ((ret = fw.open("latest.ts")) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = fw.write(buffer->bytes(), buffer->length(), NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    fw.close();
#endif
#if 0
    SrsFileReader fr;
    if ((ret = fr.open("latest.ts")) != ERROR_SUCCESS) {
        return ret;
    }
    buffer->erase(buffer->length());
    int nb_fbuf = fr.filesize();
    char* fbuf = new char[nb_fbuf];
    SrsAutoFree(char, fbuf);
    if ((ret = fr.read(fbuf, nb_fbuf, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    fr.close();
    buffer->append(fbuf, nb_fbuf);
#endif

    // find the sync byte of mpegts.
    char* p = buffer->bytes();
    for (int i = 0; i < buffer->length(); i++) {
        if (p[i] != 0x47) {
            continue;
        }

        if (i > 0) {
            buffer->erase(i);
        }
        break;
    }

    // drop ts packet when size not modulus by 188
    if (buffer->length() < SRS_TS_PACKET_SIZE) {
        srs_warn("udp: wait %s:%d packet %d/%d bytes",
            peer_ip.c_str(), peer_port, nb_buf, buffer->length());
        return ret;
    }

    // use stream to parse ts packet.
    int nb_packet =  buffer->length() / SRS_TS_PACKET_SIZE;
    for (int i = 0; i < nb_packet; i++) {
        char* p = buffer->bytes() + (i * SRS_TS_PACKET_SIZE);
        if ((ret = stream->initialize(p, SRS_TS_PACKET_SIZE)) != ERROR_SUCCESS) {
            return ret;
        }

        // process each ts packet
        if ((ret = context->decode(stream, this)) != ERROR_SUCCESS) {
            srs_warn("mpegts: ignore parse ts packet failed. ret=%d", ret);
            continue;
        }
        srs_info("mpegts: parse ts packet completed");
    }
    srs_info("mpegts: parse udp packet completed");

    // erase consumed bytes
    if (nb_packet > 0) {
        buffer->erase(nb_packet * SRS_TS_PACKET_SIZE);
    }

    return ret;
}

int SrsMpegtsOverUdp::on_ts_message(SrsTsMessage* msg)
{
    int ret = ERROR_SUCCESS;

    // about the bytes of msg, specified by elementary stream which indicates by PES_packet_data_byte and stream_id
    // for example, when SrsTsStream of SrsTsChannel indicates stream_type is SrsTsStreamVideoMpeg4 and SrsTsStreamAudioMpeg4,
    // the elementary stream can be mux in "2.11 Carriage of ISO/IEC 14496 data" in hls-mpeg-ts-iso13818-1.pdf, page 103
    // @remark, the most popular stream_id is 0xe0 for h.264 over mpegts, which indicates the stream_id is video and 
    //      stream_number is 0, where I guess the elementary is specified in 13818-2(video part).
    //      because when audio stream_number is 0, the elementary is ADTS specified in 13818-7(aac part).

    // about the bytes of PES_packet_data_byte, defined in hls-mpeg-ts-iso13818-1.pdf, page 58
    // PES_packet_data_byte ¨C PES_packet_data_bytes shall be contiguous bytes of data from the elementary stream
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
    // stream_id ¨C In Program Streams, the stream_id specifies the type and number of the elementary stream as defined by the
    // stream_id Table 2-18. In Transport Streams, the stream_id may be set to any valid value which correctly describes the
    // elementary stream type as defined in Table 2-18. In Transport Streams, the elementary stream type is specified in the
    // Program Specific Information as specified in 2.4.4.

    // about the stream_id table, define in Table 2-18 ¨C Stream_id assignments, hls-mpeg-ts-iso13818-1.pdf, page 52.
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

    srs_trace("mpegts: got %s dts=%"PRId64", pts=%"PRId64", size=%d, us=%d, cc=%d, sid=%#x(%s-%d)",
        (msg->channel->apply == SrsTsPidApplyVideo)? "Video":"Audio", msg->dts, msg->pts, msg->payload->length(),
        msg->packet->payload_unit_start_indicator, msg->continuity_counter, msg->sid,
        msg->is_audio()? "A":msg->is_video()? "V":"N", msg->stream_number());

    // TODO: FIXME: implements it.
    return ret;
}

#endif
