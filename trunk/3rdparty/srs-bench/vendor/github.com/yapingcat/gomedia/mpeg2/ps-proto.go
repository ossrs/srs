package mpeg2

import (
    "encoding/binary"
    "fmt"
    "os"

    "github.com/yapingcat/gomedia/codec"
)

type Error interface {
    NeedMore() bool
    ParserError() bool
    StreamIdNotFound() bool
}

var errNeedMore error = &needmoreError{}

type needmoreError struct{}

func (e *needmoreError) Error() string          { return "need more bytes" }
func (e *needmoreError) NeedMore() bool         { return true }
func (e *needmoreError) ParserError() bool      { return false }
func (e *needmoreError) StreamIdNotFound() bool { return false }

var errParser error = &parserError{}

type parserError struct{}

func (e *parserError) Error() string          { return "parser packet error" }
func (e *parserError) NeedMore() bool         { return false }
func (e *parserError) ParserError() bool      { return true }
func (e *parserError) StreamIdNotFound() bool { return false }

var errNotFound error = &sidNotFoundError{}

type sidNotFoundError struct{}

func (e *sidNotFoundError) Error() string          { return "stream id not found" }
func (e *sidNotFoundError) NeedMore() bool         { return false }
func (e *sidNotFoundError) ParserError() bool      { return false }
func (e *sidNotFoundError) StreamIdNotFound() bool { return true }

type PS_STREAM_TYPE int

const (
    PS_STREAM_UNKNOW PS_STREAM_TYPE = 0xFF
    PS_STREAM_AAC    PS_STREAM_TYPE = 0x0F
    PS_STREAM_H264   PS_STREAM_TYPE = 0x1B
    PS_STREAM_H265   PS_STREAM_TYPE = 0x24
    PS_STREAM_G711A  PS_STREAM_TYPE = 0x90
    PS_STREAM_G711U  PS_STREAM_TYPE = 0x91
)

// Table 2-33 – Program Stream pack header
// pack_header() {
//     pack_start_code                                     32      bslbf
//     '01'                                                2         bslbf
//     system_clock_reference_base [32..30]                 3         bslbf
//     marker_bit                                           1         bslbf
//     system_clock_reference_base [29..15]                 15         bslbf
//     marker_bit                                           1         bslbf
//     system_clock_reference_base [14..0]                  15         bslbf
//     marker_bit                                           1         bslbf
//     system_clock_reference_extension                     9         uimsbf
//     marker_bit                                           1         bslbf
//     program_mux_rate                                     22        uimsbf
//     marker_bit                                           1        bslbf
//     marker_bit                                           1        bslbf
//     reserved                                             5        bslbf
//     pack_stuffing_length                                 3        uimsbf
//     for (i = 0; i < pack_stuffing_length; i++) {
//             stuffing_byte                               8       bslbf
//     }
//     if (nextbits() == system_header_start_code) {
//             system_header ()
//     }
// }

type PSPackHeader struct {
    IsMpeg1                          bool
    System_clock_reference_base      uint64 //33 bits
    System_clock_reference_extension uint16 //9 bits
    Program_mux_rate                 uint32 //22 bits
    Pack_stuffing_length             uint8  //3 bitss
}

func (ps_pkg_hdr *PSPackHeader) PrettyPrint(file *os.File) {
    file.WriteString(fmt.Sprintf("IsMpeg1:%t\n", ps_pkg_hdr.IsMpeg1))
    file.WriteString(fmt.Sprintf("System_clock_reference_base:%d\n", ps_pkg_hdr.System_clock_reference_base))
    file.WriteString(fmt.Sprintf("System_clock_reference_extension:%d\n", ps_pkg_hdr.System_clock_reference_extension))
    file.WriteString(fmt.Sprintf("Program_mux_rate:%d\n", ps_pkg_hdr.Program_mux_rate))
    file.WriteString(fmt.Sprintf("Pack_stuffing_length:%d\n", ps_pkg_hdr.Pack_stuffing_length))
}

func (ps_pkg_hdr *PSPackHeader) Decode(bs *codec.BitStream) error {
    if bs.RemainBytes() < 5 {
        return errNeedMore
    }
    if bs.Uint32(32) != 0x000001BA {
        panic("ps header must start with 000001BA")
    }

    if bs.NextBits(2) == 0x01 { //mpeg2
        if bs.RemainBytes() < 10 {
            return errNeedMore
        }
        return ps_pkg_hdr.decodeMpeg2(bs)
    } else if bs.NextBits(4) == 0x02 { //mpeg1
        if bs.RemainBytes() < 8 {
            return errNeedMore
        }
        ps_pkg_hdr.IsMpeg1 = true
        return ps_pkg_hdr.decodeMpeg1(bs)
    } else {
        return errParser
    }
}

func (ps_pkg_hdr *PSPackHeader) decodeMpeg2(bs *codec.BitStream) error {
    bs.SkipBits(2)
    ps_pkg_hdr.System_clock_reference_base = bs.GetBits(3)
    bs.SkipBits(1)
    ps_pkg_hdr.System_clock_reference_base = ps_pkg_hdr.System_clock_reference_base<<15 | bs.GetBits(15)
    bs.SkipBits(1)
    ps_pkg_hdr.System_clock_reference_base = ps_pkg_hdr.System_clock_reference_base<<15 | bs.GetBits(15)
    bs.SkipBits(1)
    ps_pkg_hdr.System_clock_reference_extension = bs.Uint16(9)
    bs.SkipBits(1)
    ps_pkg_hdr.Program_mux_rate = bs.Uint32(22)
    bs.SkipBits(1)
    bs.SkipBits(1)
    bs.SkipBits(5)
    ps_pkg_hdr.Pack_stuffing_length = bs.Uint8(3)
    if bs.RemainBytes() < int(ps_pkg_hdr.Pack_stuffing_length) {
        bs.UnRead(10 * 8)
        return errNeedMore
    }
    bs.SkipBits(int(ps_pkg_hdr.Pack_stuffing_length) * 8)
    return nil
}

func (ps_pkg_hdr *PSPackHeader) decodeMpeg1(bs *codec.BitStream) error {
    bs.SkipBits(4)
    ps_pkg_hdr.System_clock_reference_base = bs.GetBits(3)
    bs.SkipBits(1)
    ps_pkg_hdr.System_clock_reference_base = ps_pkg_hdr.System_clock_reference_base<<15 | bs.GetBits(15)
    bs.SkipBits(1)
    ps_pkg_hdr.System_clock_reference_base = ps_pkg_hdr.System_clock_reference_base<<15 | bs.GetBits(15)
    bs.SkipBits(1)
    ps_pkg_hdr.System_clock_reference_extension = 1
    ps_pkg_hdr.Program_mux_rate = bs.Uint32(7)
    bs.SkipBits(1)
    ps_pkg_hdr.Program_mux_rate = ps_pkg_hdr.Program_mux_rate<<15 | bs.Uint32(15)
    bs.SkipBits(1)
    return nil
}

func (ps_pkg_hdr *PSPackHeader) Encode(bsw *codec.BitStreamWriter) {
    bsw.PutBytes([]byte{0x00, 0x00, 0x01, 0xBA})
    bsw.PutUint8(1, 2)
    bsw.PutUint64(ps_pkg_hdr.System_clock_reference_base>>30, 3)
    bsw.PutUint8(1, 1)
    bsw.PutUint64(ps_pkg_hdr.System_clock_reference_base>>15, 15)
    bsw.PutUint8(1, 1)
    bsw.PutUint64(ps_pkg_hdr.System_clock_reference_base, 15)
    bsw.PutUint8(1, 1)
    bsw.PutUint16(ps_pkg_hdr.System_clock_reference_extension, 9)
    bsw.PutUint8(1, 1)
    bsw.PutUint32(ps_pkg_hdr.Program_mux_rate, 22)
    bsw.PutUint8(1, 1)
    bsw.PutUint8(1, 1)
    bsw.PutUint8(0x1F, 5)
    bsw.PutUint8(ps_pkg_hdr.Pack_stuffing_length, 3)
    bsw.PutRepetValue(0xFF, int(ps_pkg_hdr.Pack_stuffing_length))
}

type Elementary_Stream struct {
    Stream_id                uint8
    P_STD_buffer_bound_scale uint8
    P_STD_buffer_size_bound  uint16
}

func NewElementary_Stream(sid uint8) *Elementary_Stream {
    return &Elementary_Stream{
        Stream_id: sid,
    }
}

// system_header () {
//     system_header_start_code         32 bslbf
//     header_length                     16 uimsbf
//     marker_bit                         1  bslbf
//     rate_bound                         22 uimsbf
//     marker_bit                         1  bslbf
//     audio_bound                     6  uimsbf
//     fixed_flag                         1  bslbf
//     CSPS_flag                         1  bslbf
//     system_audio_lock_flag             1  bslbf
//     system_video_lock_flag             1  bslbf
//     marker_bit                      1  bslbf
//     video_bound                     5  uimsbf
//     packet_rate_restriction_flag    1  bslbf
//     reserved_bits                     7  bslbf
//     while (nextbits () == '1') {
//         stream_id                     8  uimsbf
//         '11'                         2  bslbf
//         P-STD_buffer_bound_scale     1  bslbf
//         P-STD_buffer_size_bound     13 uimsbf
//     }
// }

type System_header struct {
    Header_length                uint16
    Rate_bound                   uint32
    Audio_bound                  uint8
    Fixed_flag                   uint8
    CSPS_flag                    uint8
    System_audio_lock_flag       uint8
    System_video_lock_flag       uint8
    Video_bound                  uint8
    Packet_rate_restriction_flag uint8
    Streams                      []*Elementary_Stream
}

func (sh *System_header) PrettyPrint(file *os.File) {
    file.WriteString(fmt.Sprintf("Header_length:%d\n", sh.Header_length))
    file.WriteString(fmt.Sprintf("Rate_bound:%d\n", sh.Rate_bound))
    file.WriteString(fmt.Sprintf("Audio_bound:%d\n", sh.Audio_bound))
    file.WriteString(fmt.Sprintf("Fixed_flag:%d\n", sh.Fixed_flag))
    file.WriteString(fmt.Sprintf("CSPS_flag:%d\n", sh.CSPS_flag))
    file.WriteString(fmt.Sprintf("System_audio_lock_flag:%d\n", sh.System_audio_lock_flag))
    file.WriteString(fmt.Sprintf("System_video_lock_flag:%d\n", sh.System_video_lock_flag))
    file.WriteString(fmt.Sprintf("Video_bound:%d\n", sh.Video_bound))
    file.WriteString(fmt.Sprintf("Packet_rate_restriction_flag:%d\n", sh.Packet_rate_restriction_flag))
    for i, es := range sh.Streams {
        file.WriteString(fmt.Sprintf("----streams %d\n", i))
        file.WriteString(fmt.Sprintf("    Stream_id:%d\n", es.Stream_id))
        file.WriteString(fmt.Sprintf("    P_STD_buffer_bound_scale:%d\n", es.P_STD_buffer_bound_scale))
        file.WriteString(fmt.Sprintf("    P_STD_buffer_size_bound:%d\n", es.P_STD_buffer_size_bound))
    }
}

func (sh *System_header) Encode(bsw *codec.BitStreamWriter) {
    bsw.PutBytes([]byte{0x00, 0x00, 0x01, 0xBB})
    loc := bsw.ByteOffset()
    bsw.PutUint16(0, 16)
    bsw.Markdot()
    bsw.PutUint8(1, 1)
    bsw.PutUint32(sh.Rate_bound, 22)
    bsw.PutUint8(1, 1)
    bsw.PutUint8(sh.Audio_bound, 6)
    bsw.PutUint8(sh.Fixed_flag, 1)
    bsw.PutUint8(sh.CSPS_flag, 1)
    bsw.PutUint8(sh.System_audio_lock_flag, 1)
    bsw.PutUint8(sh.System_video_lock_flag, 1)
    bsw.PutUint8(1, 1)
    bsw.PutUint8(sh.Video_bound, 5)
    bsw.PutUint8(sh.Packet_rate_restriction_flag, 1)
    bsw.PutUint8(0x7F, 7)
    for _, stream := range sh.Streams {
        bsw.PutUint8(stream.Stream_id, 8)
        bsw.PutUint8(3, 2)
        bsw.PutUint8(stream.P_STD_buffer_bound_scale, 1)
        bsw.PutUint16(stream.P_STD_buffer_size_bound, 13)
    }
    length := bsw.DistanceFromMarkDot() / 8
    bsw.SetUint16(uint16(length), loc)
}

func (sh *System_header) Decode(bs *codec.BitStream) error {
    if bs.RemainBytes() < 12 {
        return errNeedMore
    }
    if bs.Uint32(32) != 0x000001BB {
        panic("system header must start with 000001BB")
    }
    sh.Header_length = bs.Uint16(16)
    if bs.RemainBytes() < int(sh.Header_length) {
        bs.UnRead(6 * 8)
        return errNeedMore
    }
    if sh.Header_length < 6 || (sh.Header_length-6)%3 != 0 {
        return errParser
    }
    bs.SkipBits(1)
    sh.Rate_bound = bs.Uint32(22)
    bs.SkipBits(1)
    sh.Audio_bound = bs.Uint8(6)
    sh.Fixed_flag = bs.Uint8(1)
    sh.CSPS_flag = bs.Uint8(1)
    sh.System_audio_lock_flag = bs.Uint8(1)
    sh.System_video_lock_flag = bs.Uint8(1)
    bs.SkipBits(1)
    sh.Video_bound = bs.Uint8(5)
    sh.Packet_rate_restriction_flag = bs.Uint8(1)
    bs.SkipBits(7)
    sh.Streams = sh.Streams[:0]
    least := sh.Header_length - 6
    for least > 0 && bs.NextBits(1) == 0x01 {
        es := new(Elementary_Stream)
        es.Stream_id = bs.Uint8(8)
        bs.SkipBits(2)
        es.P_STD_buffer_bound_scale = bs.GetBit()
        es.P_STD_buffer_size_bound = bs.Uint16(13)
        sh.Streams = append(sh.Streams, es)
        least -= 3
    }
    if least > 0 {
        return errParser
    }
    return nil
}

type Elementary_stream_elem struct {
    Stream_type                   uint8
    Elementary_stream_id          uint8
    Elementary_stream_info_length uint16
}

func NewElementary_stream_elem(stype uint8, esid uint8) *Elementary_stream_elem {
    return &Elementary_stream_elem{
        Stream_type:          stype,
        Elementary_stream_id: esid,
    }
}

// program_stream_map() {
//     packet_start_code_prefix             24     bslbf
//     map_stream_id                         8     uimsbf
//     program_stream_map_length             16     uimsbf
//     current_next_indicator                 1     bslbf
//     reserved                             2     bslbf
//     program_stream_map_version             5     uimsbf
//     reserved                             7     bslbf
//     marker_bit                             1     bslbf
//     program_stream_info_length             16     uimsbf
//     for (i = 0; i < N; i++) {
//         descriptor()
//     }
//     elementary_stream_map_length         16     uimsbf
//     for (i = 0; i < N1; i++) {
//         stream_type                         8     uimsbf
//         elementary_stream_id             8     uimsbf
//         elementary_stream_info_length     16    uimsbf
//         for (i = 0; i < N2; i++) {
//             descriptor()
//         }
//     }
//     CRC_32                                 32     rpchof
// }

type Program_stream_map struct {
    Map_stream_id                uint8
    Program_stream_map_length    uint16
    Current_next_indicator       uint8
    Program_stream_map_version   uint8
    Program_stream_info_length   uint16
    Elementary_stream_map_length uint16
    Stream_map                   []*Elementary_stream_elem
}

func (psm *Program_stream_map) PrettyPrint(file *os.File) {
    file.WriteString(fmt.Sprintf("map_stream_id:%d\n", psm.Map_stream_id))
    file.WriteString(fmt.Sprintf("program_stream_map_length:%d\n", psm.Program_stream_map_length))
    file.WriteString(fmt.Sprintf("current_next_indicator:%d\n", psm.Current_next_indicator))
    file.WriteString(fmt.Sprintf("program_stream_map_version:%d\n", psm.Program_stream_map_version))
    file.WriteString(fmt.Sprintf("program_stream_info_length:%d\n", psm.Program_stream_info_length))
    file.WriteString(fmt.Sprintf("elementary_stream_map_length:%d\n", psm.Elementary_stream_map_length))
    for i, es := range psm.Stream_map {
        file.WriteString(fmt.Sprintf("----ES stream %d\n", i))
        if es.Stream_type == uint8(PS_STREAM_AAC) {
            file.WriteString("    stream_type:AAC\n")
        } else if es.Stream_type == uint8(PS_STREAM_G711A) {
            file.WriteString("    stream_type:G711A\n")
        } else if es.Stream_type == uint8(PS_STREAM_G711U) {
            file.WriteString("    stream_type:G711U\n")
        } else if es.Stream_type == uint8(PS_STREAM_H264) {
            file.WriteString("    stream_type:H264\n")
        } else if es.Stream_type == uint8(PS_STREAM_H265) {
            file.WriteString("    stream_type:H265\n")
        }
        file.WriteString(fmt.Sprintf("    elementary_stream_id:%d\n", es.Elementary_stream_id))
        file.WriteString(fmt.Sprintf("    elementary_stream_info_length:%d\n", es.Elementary_stream_info_length))
    }
}

func (psm *Program_stream_map) Encode(bsw *codec.BitStreamWriter) {
    bsw.PutBytes([]byte{0x00, 0x00, 0x01, 0xBC})
    loc := bsw.ByteOffset()
    bsw.PutUint16(psm.Program_stream_map_length, 16)
    bsw.Markdot()
    bsw.PutUint8(psm.Current_next_indicator, 1)
    bsw.PutUint8(3, 2)
    bsw.PutUint8(psm.Program_stream_map_version, 5)
    bsw.PutUint8(0x7F, 7)
    bsw.PutUint8(1, 1)
    bsw.PutUint16(0, 16)
    psm.Elementary_stream_map_length = uint16(len(psm.Stream_map) * 4)
    bsw.PutUint16(psm.Elementary_stream_map_length, 16)
    for _, streaminfo := range psm.Stream_map {
        bsw.PutUint8(streaminfo.Stream_type, 8)
        bsw.PutUint8(streaminfo.Elementary_stream_id, 8)
        bsw.PutUint16(0, 16)
    }
    length := bsw.DistanceFromMarkDot()/8 + 4
    bsw.SetUint16(uint16(length), loc)
    crc := codec.CalcCrc32(0xffffffff, bsw.Bits()[bsw.ByteOffset()-int(length-4)-4:bsw.ByteOffset()])
    tmpcrc := make([]byte, 4)
    binary.LittleEndian.PutUint32(tmpcrc, crc)
    bsw.PutBytes(tmpcrc)
}

func (psm *Program_stream_map) Decode(bs *codec.BitStream) error {
    if bs.RemainBytes() < 16 {
        return errNeedMore
    }
    if bs.Uint32(24) != 0x000001 {
        panic("program stream map must startwith 0x000001")
    }
    psm.Map_stream_id = bs.Uint8(8)
    if psm.Map_stream_id != 0xBC {
        panic("map stream id must be 0xBC")
    }
    psm.Program_stream_map_length = bs.Uint16(16)
    if bs.RemainBytes() < int(psm.Program_stream_map_length) {
        bs.UnRead(6 * 8)
        return errNeedMore
    }
    psm.Current_next_indicator = bs.Uint8(1)
    bs.SkipBits(2)
    psm.Program_stream_map_version = bs.Uint8(5)
    bs.SkipBits(8)
    psm.Program_stream_info_length = bs.Uint16(16)
    if bs.RemainBytes() < int(psm.Program_stream_info_length)+2 {
        bs.UnRead(10 * 8)
        return errNeedMore
    }
    bs.SkipBits(int(psm.Program_stream_info_length) * 8)
    psm.Elementary_stream_map_length = bs.Uint16(16)
    if psm.Program_stream_map_length != 6+psm.Program_stream_info_length+psm.Elementary_stream_map_length+4 {
        return errParser
    }
    if bs.RemainBytes() < int(psm.Elementary_stream_map_length)+4 {
        bs.UnRead(12*8 + int(psm.Program_stream_info_length)*8)
        return errNeedMore
    }

    i := 0
    psm.Stream_map = psm.Stream_map[:0]
    for i < int(psm.Elementary_stream_map_length) {
        elem := new(Elementary_stream_elem)
        elem.Stream_type = bs.Uint8(8)
        elem.Elementary_stream_id = bs.Uint8(8)
        elem.Elementary_stream_info_length = bs.Uint16(16)
        //TODO Parser descriptor
        if bs.RemainBytes() < int(elem.Elementary_stream_info_length) {
            return errParser
        }
        bs.SkipBits(int(elem.Elementary_stream_info_length) * 8)
        i += int(4 + elem.Elementary_stream_info_length)
        psm.Stream_map = append(psm.Stream_map, elem)
    }

    if i != int(psm.Elementary_stream_map_length) {
        return errParser
    }

    bs.SkipBits(32)
    return nil
}

type Program_stream_directory struct {
    PES_packet_length uint16
}

func (psd *Program_stream_directory) Decode(bs *codec.BitStream) error {
    if bs.RemainBytes() < 6 {
        return errNeedMore
    }
    if bs.Uint32(32) != 0x000001FF {
        panic("program stream directory 000001FF")
    }
    psd.PES_packet_length = bs.Uint16(16)
    if bs.RemainBytes() < int(psd.PES_packet_length) {
        bs.UnRead(6 * 8)
        return errNeedMore
    }
    //TODO Program Stream directory
    bs.SkipBits(int(psd.PES_packet_length) * 8)
    return nil
}

type CommonPesPacket struct {
    Stream_id         uint8
    PES_packet_length uint16
}

func (compes *CommonPesPacket) Decode(bs *codec.BitStream) error {
    if bs.RemainBytes() < 6 {
        return errNeedMore
    }
    bs.SkipBits(24)
    compes.Stream_id = bs.Uint8(8)
    compes.PES_packet_length = bs.Uint16(16)
    if bs.RemainBytes() < int(compes.PES_packet_length) {
        bs.UnRead(6 * 8)
        return errNeedMore
    }
    bs.SkipBits(int(compes.PES_packet_length) * 8)
    return nil
}

type PSPacket struct {
    Header  *PSPackHeader
    System  *System_header
    Psm     *Program_stream_map
    Psd     *Program_stream_directory
    CommPes *CommonPesPacket
    Pes     *PesPacket
}
