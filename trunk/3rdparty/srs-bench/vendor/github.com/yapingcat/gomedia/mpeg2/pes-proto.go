package mpeg2

import (
    "fmt"
    "os"

    "github.com/yapingcat/gomedia/codec"
)

var H264_AUD_NALU []byte = []byte{0x00, 0x00, 0x00, 0x01, 0x09, 0xF0} //ffmpeg mpegtsenc.c mpegts_write_packet_internal
var H265_AUD_NALU []byte = []byte{0x00, 0x00, 0x00, 0x01, 0x46, 0x01, 0x50}

type PES_STREMA_ID int

const (
    PES_STREAM_END         PES_STREMA_ID = 0xB9
    PES_STREAM_START       PES_STREMA_ID = 0xBA
    PES_STREAM_SYSTEM_HEAD PES_STREMA_ID = 0xBB
    PES_STREAM_MAP         PES_STREMA_ID = 0xBC
    PES_STREAM_PRIVATE     PES_STREMA_ID = 0xBD
    PES_STREAM_AUDIO       PES_STREMA_ID = 0xC0
    PES_STREAM_VIDEO       PES_STREMA_ID = 0xE0
)

func findPESIDByStreamType(cid TS_STREAM_TYPE) PES_STREMA_ID {
    if cid == TS_STREAM_AAC {
        return PES_STREAM_AUDIO
    } else if cid == TS_STREAM_H264 || cid == TS_STREAM_H265 {
        return PES_STREAM_VIDEO
    } else {
        return PES_STREAM_PRIVATE
    }
}

type PesPacket struct {
    Stream_id                 uint8
    PES_packet_length         uint16
    PES_scrambling_control    uint8
    PES_priority              uint8
    Data_alignment_indicator  uint8
    Copyright                 uint8
    Original_or_copy          uint8
    PTS_DTS_flags             uint8
    ESCR_flag                 uint8
    ES_rate_flag              uint8
    DSM_trick_mode_flag       uint8
    Additional_copy_info_flag uint8
    PES_CRC_flag              uint8
    PES_extension_flag        uint8
    PES_header_data_length    uint8
    Pts                       uint64
    Dts                       uint64
    ESCR_base                 uint64
    ESCR_extension            uint16
    ES_rate                   uint32
    Trick_mode_control        uint8
    Trick_value               uint8
    Additional_copy_info      uint8
    Previous_PES_packet_CRC   uint16
    Pes_payload               []byte
    //TODO
    //if ( PES_extension_flag == '1')
    // PES_private_data_flag                uint8
    // pack_header_field_flag               uint8
    // program_packet_sequence_counter_flag uint8
    // P_STD_buffer_flag                    uint8
    // PES_extension_flag_2                 uint8
    // PES_private_data                     [16]byte
}

func NewPesPacket() *PesPacket {
    return new(PesPacket)
}

func (pkg *PesPacket) PrettyPrint(file *os.File) {
    file.WriteString(fmt.Sprintf("stream_id:%d\n", pkg.Stream_id))
    file.WriteString(fmt.Sprintf("PES_packet_length:%d\n", pkg.PES_packet_length))
    file.WriteString(fmt.Sprintf("PES_scrambling_control:%d\n", pkg.PES_scrambling_control))
    file.WriteString(fmt.Sprintf("PES_priority:%d\n", pkg.PES_priority))
    file.WriteString(fmt.Sprintf("data_alignment_indicator:%d\n", pkg.Data_alignment_indicator))
    file.WriteString(fmt.Sprintf("copyright:%d\n", pkg.Copyright))
    file.WriteString(fmt.Sprintf("original_or_copy:%d\n", pkg.Original_or_copy))
    file.WriteString(fmt.Sprintf("PTS_DTS_flags:%d\n", pkg.PTS_DTS_flags))
    file.WriteString(fmt.Sprintf("ESCR_flag:%d\n", pkg.ESCR_flag))
    file.WriteString(fmt.Sprintf("ES_rate_flag:%d\n", pkg.ES_rate_flag))
    file.WriteString(fmt.Sprintf("DSM_trick_mode_flag:%d\n", pkg.DSM_trick_mode_flag))
    file.WriteString(fmt.Sprintf("additional_copy_info_flag:%d\n", pkg.Additional_copy_info_flag))
    file.WriteString(fmt.Sprintf("PES_CRC_flag:%d\n", pkg.PES_CRC_flag))
    file.WriteString(fmt.Sprintf("PES_extension_flag:%d\n", pkg.PES_extension_flag))
    file.WriteString(fmt.Sprintf("PES_header_data_length:%d\n", pkg.PES_header_data_length))
    if pkg.PTS_DTS_flags&0x02 == 0x02 {
        file.WriteString(fmt.Sprintf("PTS:%d\n", pkg.Pts))
    }
    if pkg.PTS_DTS_flags&0x03 == 0x03 {
        file.WriteString(fmt.Sprintf("DTS:%d\n", pkg.Dts))
    }

    if pkg.ESCR_flag == 1 {
        file.WriteString(fmt.Sprintf("ESCR_base:%d\n", pkg.ESCR_base))
        file.WriteString(fmt.Sprintf("ESCR_extension:%d\n", pkg.ESCR_extension))
    }

    if pkg.ES_rate_flag == 1 {
        file.WriteString(fmt.Sprintf("ES_rate:%d\n", pkg.ES_rate))
    }

    if pkg.DSM_trick_mode_flag == 1 {
        file.WriteString(fmt.Sprintf("trick_mode_control:%d\n", pkg.Trick_mode_control))
    }

    if pkg.Additional_copy_info_flag == 1 {
        file.WriteString(fmt.Sprintf("additional_copy_info:%d\n", pkg.Additional_copy_info))
    }

    if pkg.PES_CRC_flag == 1 {
        file.WriteString(fmt.Sprintf("previous_PES_packet_CRC:%d\n", pkg.Previous_PES_packet_CRC))
    }
    file.WriteString("PES_packet_data_byte:\n")
    file.WriteString(fmt.Sprintf("  Size: %d\n", len(pkg.Pes_payload)))
    file.WriteString("  data:")
    for i := 0; i < 12 && i < len(pkg.Pes_payload); i++ {
        if i%4 == 0 {
            file.WriteString("\n")
            file.WriteString("      ")
        }
        file.WriteString(fmt.Sprintf("0x%02x ", pkg.Pes_payload[i]))
    }
    file.WriteString("\n")
}

func (pkg *PesPacket) Decode(bs *codec.BitStream) error {
    if bs.RemainBytes() < 9 {
        return errNeedMore
    }
    bs.SkipBits(24)             //packet_start_code_prefix
    pkg.Stream_id = bs.Uint8(8) //stream_id
    pkg.PES_packet_length = bs.Uint16(16)
    bs.SkipBits(2) //'10'
    pkg.PES_scrambling_control = bs.Uint8(2)
    pkg.PES_priority = bs.Uint8(1)
    pkg.Data_alignment_indicator = bs.Uint8(1)
    pkg.Copyright = bs.Uint8(1)
    pkg.Original_or_copy = bs.Uint8(1)
    pkg.PTS_DTS_flags = bs.Uint8(2)
    pkg.ESCR_flag = bs.Uint8(1)
    pkg.ES_rate_flag = bs.Uint8(1)
    pkg.DSM_trick_mode_flag = bs.Uint8(1)
    pkg.Additional_copy_info_flag = bs.Uint8(1)
    pkg.PES_CRC_flag = bs.Uint8(1)
    pkg.PES_extension_flag = bs.Uint8(1)
    pkg.PES_header_data_length = bs.Uint8(8)
    if bs.RemainBytes() < int(pkg.PES_header_data_length) {
        bs.UnRead(9 * 8)
        return errNeedMore
    }
    bs.Markdot()
    if pkg.PTS_DTS_flags&0x02 == 0x02 {
        bs.SkipBits(4)
        pkg.Pts = bs.GetBits(3)
        bs.SkipBits(1)
        pkg.Pts = (pkg.Pts << 15) | bs.GetBits(15)
        bs.SkipBits(1)
        pkg.Pts = (pkg.Pts << 15) | bs.GetBits(15)
        bs.SkipBits(1)
    }
    if pkg.PTS_DTS_flags&0x03 == 0x03 {
        bs.SkipBits(4)
        pkg.Dts = bs.GetBits(3)
        bs.SkipBits(1)
        pkg.Dts = (pkg.Dts << 15) | bs.GetBits(15)
        bs.SkipBits(1)
        pkg.Dts = (pkg.Dts << 15) | bs.GetBits(15)
        bs.SkipBits(1)
    } else {
        pkg.Dts = pkg.Pts
    }

    if pkg.ESCR_flag == 1 {
        bs.SkipBits(2)
        pkg.ESCR_base = bs.GetBits(3)
        bs.SkipBits(1)
        pkg.ESCR_base = (pkg.Pts << 15) | bs.GetBits(15)
        bs.SkipBits(1)
        pkg.ESCR_base = (pkg.Pts << 15) | bs.GetBits(15)
        bs.SkipBits(1)
        pkg.ESCR_extension = bs.Uint16(9)
        bs.SkipBits(1)
    }

    if pkg.ES_rate_flag == 1 {
        bs.SkipBits(1)
        pkg.ES_rate = bs.Uint32(22)
        bs.SkipBits(1)
    }

    if pkg.DSM_trick_mode_flag == 1 {
        pkg.Trick_mode_control = bs.Uint8(3)
        pkg.Trick_value = bs.Uint8(5)
    }

    if pkg.Additional_copy_info_flag == 1 {
        pkg.Additional_copy_info = bs.Uint8(7)
    }

    if pkg.PES_CRC_flag == 1 {
        pkg.Previous_PES_packet_CRC = bs.Uint16(16)
    }

    loc := bs.DistanceFromMarkDot()
    bs.SkipBits(int(pkg.PES_header_data_length)*8 - loc) // skip remaining header

    // the -3 bytes are the combined lengths
    // of all fields between PES_packet_length and PES_header_data_length (2 bytes)
    // and the PES_header_data_length itself (1 byte)
    dataLen := int(pkg.PES_packet_length - 3 - uint16(pkg.PES_header_data_length))

    if bs.RemainBytes() < dataLen {
        pkg.Pes_payload = bs.RemainData()
        bs.UnRead((9 + int(pkg.PES_header_data_length)) * 8)
        return errNeedMore
    }

    if pkg.PES_packet_length == 0 || bs.RemainBytes() <= dataLen {
        pkg.Pes_payload = bs.RemainData()
        bs.SkipBits(bs.RemainBits())
    } else {
        pkg.Pes_payload = bs.RemainData()[:dataLen]
        bs.SkipBits(dataLen * 8)
    }

    return nil
}

func (pkg *PesPacket) DecodeMpeg1(bs *codec.BitStream) error {
    if bs.RemainBytes() < 6 {
        return errNeedMore
    }
    bs.SkipBits(24)             //packet_start_code_prefix
    pkg.Stream_id = bs.Uint8(8) //stream_id
    pkg.PES_packet_length = bs.Uint16(16)
    if pkg.PES_packet_length != 0 && bs.RemainBytes() < int(pkg.PES_packet_length) {
        bs.UnRead(6 * 8)
        return errNeedMore
    }
    bs.Markdot()
    for bs.NextBits(8) == 0xFF {
        bs.SkipBits(8)
    }
    if bs.NextBits(2) == 0x01 {
        bs.SkipBits(16)
    }
    if bs.NextBits(4) == 0x02 {
        bs.SkipBits(4)
        pkg.Pts = bs.GetBits(3)
        bs.SkipBits(1)
        pkg.Pts = pkg.Pts<<15 | bs.GetBits(15)
        bs.SkipBits(1)
        pkg.Pts = pkg.Pts<<15 | bs.GetBits(15)
        bs.SkipBits(1)
    } else if bs.NextBits(4) == 0x03 {
        bs.SkipBits(4)
        pkg.Pts = bs.GetBits(3)
        bs.SkipBits(1)
        pkg.Pts = pkg.Pts<<15 | bs.GetBits(15)
        bs.SkipBits(1)
        pkg.Pts = pkg.Pts<<15 | bs.GetBits(15)
        bs.SkipBits(1)
        pkg.Dts = bs.GetBits(3)
        bs.SkipBits(1)
        pkg.Dts = pkg.Pts<<15 | bs.GetBits(15)
        bs.SkipBits(1)
        pkg.Dts = pkg.Pts<<15 | bs.GetBits(15)
        bs.SkipBits(1)
    } else if bs.NextBits(8) == 0x0F {
        bs.SkipBits(8)
    } else {
        return errParser
    }
    loc := bs.DistanceFromMarkDot() / 8
    if pkg.PES_packet_length < uint16(loc) {
        return errParser
    }
    if pkg.PES_packet_length == 0 ||
        bs.RemainBits() <= int(pkg.PES_packet_length-uint16(loc))*8 {
        pkg.Pes_payload = bs.RemainData()
        bs.SkipBits(bs.RemainBits())
    } else {
        pkg.Pes_payload = bs.RemainData()[:pkg.PES_packet_length-uint16(loc)]
        bs.SkipBits(int(pkg.PES_packet_length-uint16(loc)) * 8)
    }
    return nil
}

func (pkg *PesPacket) Encode(bsw *codec.BitStreamWriter) {
    bsw.PutBytes([]byte{0x00, 0x00, 0x01})
    bsw.PutByte(pkg.Stream_id)
    bsw.PutUint16(pkg.PES_packet_length, 16)
    bsw.PutUint8(0x02, 2)
    bsw.PutUint8(pkg.PES_scrambling_control, 2)
    bsw.PutUint8(pkg.PES_priority, 1)
    bsw.PutUint8(pkg.Data_alignment_indicator, 1)
    bsw.PutUint8(pkg.Copyright, 1)
    bsw.PutUint8(pkg.Original_or_copy, 1)
    bsw.PutUint8(pkg.PTS_DTS_flags, 2)
    bsw.PutUint8(pkg.ESCR_flag, 1)
    bsw.PutUint8(pkg.ES_rate_flag, 1)
    bsw.PutUint8(pkg.DSM_trick_mode_flag, 1)
    bsw.PutUint8(pkg.Additional_copy_info_flag, 1)
    bsw.PutUint8(pkg.PES_CRC_flag, 1)
    bsw.PutUint8(pkg.PES_extension_flag, 1)
    bsw.PutByte(pkg.PES_header_data_length)
    if pkg.PTS_DTS_flags == 0x02 {
        bsw.PutUint8(0x02, 4)
        bsw.PutUint64(pkg.Pts>>30, 3)
        bsw.PutUint8(0x01, 1)
        bsw.PutUint64(pkg.Pts>>15, 15)
        bsw.PutUint8(0x01, 1)
        bsw.PutUint64(pkg.Pts, 15)
        bsw.PutUint8(0x01, 1)
    }

    if pkg.PTS_DTS_flags == 0x03 {
        bsw.PutUint8(0x03, 4)
        bsw.PutUint64(pkg.Pts>>30, 3)
        bsw.PutUint8(0x01, 1)
        bsw.PutUint64(pkg.Pts>>15, 15)
        bsw.PutUint8(0x01, 1)
        bsw.PutUint64(pkg.Pts, 15)
        bsw.PutUint8(0x01, 1)
        bsw.PutUint8(0x01, 4)
        bsw.PutUint64(pkg.Dts>>30, 3)
        bsw.PutUint8(0x01, 1)
        bsw.PutUint64(pkg.Dts>>15, 15)
        bsw.PutUint8(0x01, 1)
        bsw.PutUint64(pkg.Dts, 15)
        bsw.PutUint8(0x01, 1)
    }

    if pkg.ESCR_flag == 1 {
        bsw.PutUint8(0x03, 2)
        bsw.PutUint64(pkg.ESCR_base>>30, 3)
        bsw.PutUint8(0x01, 1)
        bsw.PutUint64(pkg.ESCR_base>>15, 15)
        bsw.PutUint8(0x01, 1)
        bsw.PutUint64(pkg.ESCR_base, 15)
        bsw.PutUint8(0x01, 1)
    }
    bsw.PutBytes(pkg.Pes_payload)
}
