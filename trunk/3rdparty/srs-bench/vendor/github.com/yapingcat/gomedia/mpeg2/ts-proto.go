package mpeg2

import (
    "encoding/binary"
    "errors"
    "fmt"
    "os"

    "github.com/yapingcat/gomedia/codec"
)

//PID
type TS_PID int

const (
    TS_PID_PAT TS_PID = 0x0000
    TS_PID_CAT
    TS_PID_TSDT
    TS_PID_IPMP
    TS_PID_Nil = 0x1FFFF
)

//Table id
type PAT_TID int

const (
    TS_TID_PAS       PAT_TID = 0x00 // program_association_section
    TS_TID_CAS               = 0x01 // conditional_access_section(CA_section)
    TS_TID_PMS               = 0x02 // TS_program_map_section
    TS_TID_SDS               = 0x03 //TS_description_section
    TS_TID_FORBIDDEN PAT_TID = 0xFF
)

type TS_STREAM_TYPE int

const (
    TS_STREAM_AAC  TS_STREAM_TYPE = 0x0F
    TS_STREAM_H264 TS_STREAM_TYPE = 0x1B
    TS_STREAM_H265 TS_STREAM_TYPE = 0x24
)

const (
    TS_PAKCET_SIZE = 188
)

type Display interface {
    PrettyPrint(file *os.File)
}

// transport_packet(){
//     sync_byte                                                                                         8                      bslbf
//     transport_error_indicator                                                                         1                      bslbf
//     payload_unit_start_indicator                                                                     1                      bslbf
//     transport_priority                                                                                 1                      bslbf
//     PID                                                                                             13                     uimsbf
//     transport_scrambling_control                                                                     2                      bslbf
//     adaptation_field_control                                                                        2                      bslbf
//     continuity_counter                                                                                 4                      uimsbf
//     if(adaptation_field_control = = '10' || adaptation_field_control = = '11'){
//          adaptation_field()
//      }
//      if(adaptation_field_control = = '01' || adaptation_field_control = = '11') {
//          for (i = 0; i < N; i++){
//              data_byte                                                                                 8                      bslbf
//          }
//     }
// }

type TSPacket struct {
    Transport_error_indicator    uint8
    Payload_unit_start_indicator uint8
    Transport_priority           uint8
    PID                          uint16
    Transport_scrambling_control uint8
    Adaptation_field_control     uint8
    Continuity_counter           uint8
    Field                        *Adaptation_field
    Payload                      interface{}
}

func (pkg *TSPacket) PrettyPrint(file *os.File) {
    file.WriteString(fmt.Sprintf("Transport_error_indicator:%d\n", pkg.Transport_error_indicator))
    file.WriteString(fmt.Sprintf("Payload_unit_start_indicator:%d\n", pkg.Payload_unit_start_indicator))
    file.WriteString(fmt.Sprintf("Transport_priority:%d\n", pkg.Transport_priority))
    file.WriteString(fmt.Sprintf("PID:%d\n", pkg.PID))
    file.WriteString(fmt.Sprintf("Transport_scrambling_control:%d\n", pkg.Transport_scrambling_control))
    file.WriteString(fmt.Sprintf("Adaptation_field_control:%d\n", pkg.Adaptation_field_control))
    file.WriteString(fmt.Sprintf("Continuity_counter:%d\n", pkg.Continuity_counter))
}

func (pkg *TSPacket) EncodeHeader(bsw *codec.BitStreamWriter) {
    bsw.PutByte(0x47)
    bsw.PutUint8(pkg.Transport_error_indicator, 1)
    bsw.PutUint8(pkg.Payload_unit_start_indicator, 1)
    bsw.PutUint8(pkg.Transport_priority, 1)
    bsw.PutUint16(pkg.PID, 13)
    bsw.PutUint8(pkg.Transport_scrambling_control, 2)
    bsw.PutUint8(pkg.Adaptation_field_control, 2)
    bsw.PutUint8(pkg.Continuity_counter, 4)
    if pkg.Field != nil && (pkg.Adaptation_field_control&0x02) != 0 {
        pkg.Field.Encode(bsw)
    }
}

func (pkg *TSPacket) DecodeHeader(bs *codec.BitStream) error {
    sync_byte := bs.Uint8(8)
    if sync_byte != 0x47 {
        return errors.New("ts packet must start with 0x47")
    }
    pkg.Transport_error_indicator = bs.GetBit()
    pkg.Payload_unit_start_indicator = bs.GetBit()
    pkg.Transport_priority = bs.GetBit()
    pkg.PID = bs.Uint16(13)
    pkg.Transport_scrambling_control = bs.Uint8(2)
    pkg.Adaptation_field_control = bs.Uint8(2)
    pkg.Continuity_counter = bs.Uint8(4)
    if pkg.Adaptation_field_control == 0x02 || pkg.Adaptation_field_control == 0x03 {
        if pkg.Field == nil {
            pkg.Field = new(Adaptation_field)
        }
        err := pkg.Field.Decode(bs)
        if err != nil {
            return err
        }
    }
    return nil
}

//
// adaptation_field() {
// adaptation_field_length
// if (adaptation_field_length > 0) {
//     discontinuity_indicator
//     random_access_indicator
//     elementary_stream_priority_indicator
//     PCR_flag
//     OPCR_flag
//     splicing_point_flag
//     transport_private_data_flag
//     adaptation_field_extension_flag
//     if (PCR_flag == '1') {
//         program_clock_reference_base
//         reserved
//         program_clock_reference_extension
//     }
//     if (OPCR_flag == '1') {
//         original_program_clock_reference_base
//         reserved
//         original_program_clock_reference_extension
//     }
//     if (splicing_point_flag == '1') {
//         splice_countdown
//     }
//     if (transport_private_data_flag == '1') {
//         transport_private_data_length
//         for (i = 0; i < transport_private_data_length; i++) {
//             private_data_byte
//         }
//     }
//     if (adaptation_field_extension_flag == '1') {
//         adaptation_field_extension_length
//         ltw_flag piecewise_rate_flag
//         seamless_splice_flag
//         reserved
//         if (ltw_flag == '1') {
//             ltw_valid_flag
//             ltw_offset
//         }
//         if (piecewise_rate_flag == '1') {
//             reserved
//             piecewise_rate
//         }
//         if (seamless_splice_flag == '1') {
//             splice_type
//             DTS_next_AU[32..30]
//             marker_bit
//             DTS_next_AU[29..15]
//             marker_bit
//             DTS_next_AU[14..0]
//             marker_bit 1
//         }
//         for (i = 0; i < N; i++) {
//             reserved 8
//         }
//     }
//     for (i = 0; i < N; i++) {
//         stuffing_byte 8
//     }
// }

type Adaptation_field struct {
    SingleStuffingByte                         bool   // The value 0 is for inserting a single stuffing byte in a Transport Stream packet
    Adaptation_field_length                    uint8  //8   uimsbf
    Discontinuity_indicator                    uint8  //1   bslbf
    Random_access_indicator                    uint8  //1   bslbf
    Elementary_stream_priority_indicator       uint8  //1   bslbf
    PCR_flag                                   uint8  //1   bslbf
    OPCR_flag                                  uint8  //1   bslbf
    Splicing_point_flag                        uint8  //1   bslbf
    Transport_private_data_flag                uint8  //1   bslbf
    Adaptation_field_extension_flag            uint8  //1   bslbf
    Program_clock_reference_base               uint64 //33  uimsbf
    Program_clock_reference_extension          uint16 //9   uimsbf
    Original_program_clock_reference_base      uint64 //33  uimsbf
    Original_program_clock_reference_extension uint16 //9   uimsbf
    Splice_countdown                           uint8  //8   uimsbf
    Transport_private_data_length              uint8  //8   uimsbf
    Adaptation_field_extension_length          uint8  //8   uimsbf
    Ltw_flag                                   uint8  //1   bslbf
    Piecewise_rate_flag                        uint8  //1   bslbf
    Seamless_splice_flag                       uint8  //1   bslbf
    Ltw_valid_flag                             uint8  //1   bslbf
    Ltw_offset                                 uint16 //15  uimsbf
    Piecewise_rate                             uint32 //22  uimsbf
    Splice_type                                uint8  //4   uimsbf
    DTS_next_AU                                uint64
    Stuffing_byte                              uint8
}

func (adaptation *Adaptation_field) PrettyPrint(file *os.File) {
    file.WriteString(fmt.Sprintf("Adaptation_field_length:%d\n", adaptation.Adaptation_field_length))
    file.WriteString(fmt.Sprintf("Discontinuity_indicator:%d\n", adaptation.Discontinuity_indicator))
    file.WriteString(fmt.Sprintf("Random_access_indicator:%d\n", adaptation.Random_access_indicator))
    file.WriteString(fmt.Sprintf("Elementary_stream_priority_indicator:%d\n", adaptation.Elementary_stream_priority_indicator))
    file.WriteString(fmt.Sprintf("PCR_flag:%d\n", adaptation.PCR_flag))
    file.WriteString(fmt.Sprintf("OPCR_flag:%d\n", adaptation.OPCR_flag))
    file.WriteString(fmt.Sprintf("Splicing_point_flag:%d\n", adaptation.Splicing_point_flag))
    file.WriteString(fmt.Sprintf("Transport_private_data_flag:%d\n", adaptation.Transport_private_data_flag))
    file.WriteString(fmt.Sprintf("Adaptation_field_extension_flag:%d\n", adaptation.Adaptation_field_extension_flag))
    if adaptation.PCR_flag == 1 {
        file.WriteString(fmt.Sprintf("Program_clock_reference_base:%d\n", adaptation.Program_clock_reference_base))
        file.WriteString(fmt.Sprintf("Program_clock_reference_extension:%d\n", adaptation.Program_clock_reference_extension))
    }
    if adaptation.OPCR_flag == 1 {
        file.WriteString(fmt.Sprintf("Original_program_clock_reference_base:%d\n", adaptation.Original_program_clock_reference_base))
        file.WriteString(fmt.Sprintf("Original_program_clock_reference_extension:%d\n", adaptation.Original_program_clock_reference_extension))
    }
    if adaptation.Splicing_point_flag == 1 {
        file.WriteString(fmt.Sprintf("Splice_countdown:%d\n", adaptation.Splice_countdown))
    }
    if adaptation.Transport_private_data_flag == 1 {
        file.WriteString(fmt.Sprintf("Transport_private_data_length:%d\n", adaptation.Transport_private_data_length))
    }
    if adaptation.Adaptation_field_extension_flag == 1 {
        file.WriteString(fmt.Sprintf("Adaptation_field_extension_length:%d\n", adaptation.Adaptation_field_extension_length))
        file.WriteString(fmt.Sprintf("Ltw_flag:%d\n", adaptation.Ltw_flag))
        file.WriteString(fmt.Sprintf("Piecewise_rate_flag:%d\n", adaptation.Piecewise_rate_flag))
        file.WriteString(fmt.Sprintf("Seamless_splice_flag:%d\n", adaptation.Seamless_splice_flag))
        if adaptation.Ltw_flag == 1 {
            file.WriteString(fmt.Sprintf("Ltw_valid_flag:%d\n", adaptation.Ltw_valid_flag))
            file.WriteString(fmt.Sprintf("Ltw_offset:%d\n", adaptation.Ltw_offset))
        }
        if adaptation.Piecewise_rate_flag == 1 {
            file.WriteString(fmt.Sprintf("Piecewise_rate:%d\n", adaptation.Piecewise_rate))
        }
        if adaptation.Seamless_splice_flag == 1 {
            file.WriteString(fmt.Sprintf("Splice_type:%d\n", adaptation.Splice_type))
            file.WriteString(fmt.Sprintf("DTS_next_AU:%d\n", adaptation.DTS_next_AU))
        }
    }
}

func (adaptation *Adaptation_field) Encode(bsw *codec.BitStreamWriter) {
    loc := bsw.ByteOffset()
    bsw.PutUint8(adaptation.Adaptation_field_length, 8)
    if adaptation.SingleStuffingByte {
        return
    }
    bsw.Markdot()
    bsw.PutUint8(adaptation.Discontinuity_indicator, 1)
    bsw.PutUint8(adaptation.Random_access_indicator, 1)
    bsw.PutUint8(adaptation.Elementary_stream_priority_indicator, 1)
    bsw.PutUint8(adaptation.PCR_flag, 1)
    bsw.PutUint8(adaptation.OPCR_flag, 1)
    bsw.PutUint8(adaptation.Splicing_point_flag, 1)
    bsw.PutUint8(0 /*adaptation.Transport_private_data_flag*/, 1)
    bsw.PutUint8(0 /*adaptation.Adaptation_field_extension_flag*/, 1)
    if adaptation.PCR_flag == 1 {
        bsw.PutUint64(adaptation.Program_clock_reference_base, 33)
        bsw.PutUint8(0, 6)
        bsw.PutUint16(adaptation.Program_clock_reference_extension, 9)
    }
    if adaptation.OPCR_flag == 1 {
        bsw.PutUint64(adaptation.Original_program_clock_reference_base, 33)
        bsw.PutUint8(0, 6)
        bsw.PutUint16(adaptation.Original_program_clock_reference_extension, 9)
    }
    if adaptation.Splicing_point_flag == 1 {
        bsw.PutUint8(adaptation.Splice_countdown, 8)
    }
    //TODO
    // if adaptation.Transport_private_data_flag == 0 {
    // }
    // if adaptation.Adaptation_field_extension_flag == 0 {
    // }
    adaptation.Adaptation_field_length = uint8(bsw.DistanceFromMarkDot() / 8)
    bsw.PutRepetValue(0xff, int(adaptation.Stuffing_byte))
    adaptation.Adaptation_field_length += adaptation.Stuffing_byte
    bsw.SetByte(adaptation.Adaptation_field_length, loc)
}

func (adaptation *Adaptation_field) Decode(bs *codec.BitStream) error {
    if bs.RemainBytes() < 1 {
        return errors.New("len of data < 1 byte")
    }
    adaptation.Adaptation_field_length = bs.Uint8(8)
    startoffset := bs.ByteOffset()
    //fmt.Printf("Adaptation_field_length=%d\n", adaptation.Adaptation_field_length)
    if bs.RemainBytes() < int(adaptation.Adaptation_field_length) {
        return errors.New("len of data < Adaptation_field_length")
    }
    if adaptation.Adaptation_field_length == 0 {
        return nil
    }
    adaptation.Discontinuity_indicator = bs.GetBit()
    adaptation.Random_access_indicator = bs.GetBit()
    adaptation.Elementary_stream_priority_indicator = bs.GetBit()
    adaptation.PCR_flag = bs.GetBit()
    adaptation.OPCR_flag = bs.GetBit()
    adaptation.Splicing_point_flag = bs.GetBit()
    adaptation.Transport_private_data_flag = bs.GetBit()
    adaptation.Adaptation_field_extension_flag = bs.GetBit()
    if adaptation.PCR_flag == 1 {
        adaptation.Program_clock_reference_base = bs.GetBits(33)
        bs.SkipBits(6)
        adaptation.Program_clock_reference_extension = uint16(bs.GetBits(9))
    }
    if adaptation.OPCR_flag == 1 {
        adaptation.Original_program_clock_reference_base = bs.GetBits(33)
        bs.SkipBits(6)
        adaptation.Original_program_clock_reference_extension = uint16(bs.GetBits(9))
    }
    if adaptation.Splicing_point_flag == 1 {
        adaptation.Splice_countdown = bs.Uint8(8)
    }
    if adaptation.Transport_private_data_flag == 1 {
        adaptation.Transport_private_data_length = bs.Uint8(8)
        bs.SkipBits(8 * int(adaptation.Transport_private_data_length))
    }
    if adaptation.Adaptation_field_extension_flag == 1 {
        adaptation.Adaptation_field_extension_length = bs.Uint8(8)
        bs.Markdot()
        adaptation.Ltw_flag = bs.GetBit()
        adaptation.Piecewise_rate_flag = bs.GetBit()
        adaptation.Seamless_splice_flag = bs.GetBit()
        bs.SkipBits(5)
        if adaptation.Ltw_flag == 1 {
            adaptation.Ltw_valid_flag = bs.GetBit()
            adaptation.Ltw_offset = uint16(bs.GetBits(15))
        }
        if adaptation.Piecewise_rate_flag == 1 {
            bs.SkipBits(2)
            adaptation.Piecewise_rate = uint32(bs.GetBits(22))
        }
        if adaptation.Seamless_splice_flag == 1 {
            adaptation.Splice_type = uint8(bs.GetBits(4))
            adaptation.DTS_next_AU = bs.GetBits(3)
            bs.SkipBits(1)
            adaptation.DTS_next_AU = adaptation.DTS_next_AU<<15 | bs.GetBits(15)
            bs.SkipBits(1)
            adaptation.DTS_next_AU = adaptation.DTS_next_AU<<15 | bs.GetBits(15)
            bs.SkipBits(1)
        }
        bitscount := bs.DistanceFromMarkDot()
        if bitscount%8 > 0 {
            panic("maybe parser ts file failed")
        }
        bs.SkipBits(int(adaptation.Adaptation_field_extension_length*8 - uint8(bitscount)))
    }
    endoffset := bs.ByteOffset()
    bs.SkipBits((int(adaptation.Adaptation_field_length) - (endoffset - startoffset)) * 8)
    return nil
}

type PmtPair struct {
    Program_number uint16
    PID            uint16
}

type Pat struct {
    Table_id                 uint8  //8  uimsbf
    Section_syntax_indicator uint8  //1  bslbf
    Section_length           uint16 //12 uimsbf
    Transport_stream_id      uint16 //16 uimsbf
    Version_number           uint8  //5  uimsbf
    Current_next_indicator   uint8  //1  bslbf
    Section_number           uint8  //8  uimsbf
    Last_section_number      uint8  //8  uimsbf
    Pmts                     []PmtPair
}

func NewPat() *Pat {
    return &Pat{
        Table_id: uint8(TS_TID_PAS),
        Pmts:     make([]PmtPair, 0, 8),
    }
}

func (pat *Pat) PrettyPrint(file *os.File) {
    file.WriteString(fmt.Sprintf("Table id:%d\n", pat.Table_id))
    file.WriteString(fmt.Sprintf("Section_syntax_indicator:%d\n", pat.Section_syntax_indicator))
    file.WriteString(fmt.Sprintf("Section_length:%d\n", pat.Section_length))
    file.WriteString(fmt.Sprintf("Transport_stream_id:%d\n", pat.Transport_stream_id))
    file.WriteString(fmt.Sprintf("Version_number:%d\n", pat.Version_number))
    file.WriteString(fmt.Sprintf("Current_next_indicator:%d\n", pat.Current_next_indicator))
    file.WriteString(fmt.Sprintf("Section_number:%d\n", pat.Section_number))
    file.WriteString(fmt.Sprintf("Last_section_number:%d\n", pat.Last_section_number))
    for i, pmt := range pat.Pmts {
        file.WriteString(fmt.Sprintf("----pmt %d\n", i))
        file.WriteString(fmt.Sprintf("    program_number:%d\n", pmt.Program_number))
        if pmt.Program_number == 0x0000 {
            file.WriteString(fmt.Sprintf("    network_PID:%d\n", pmt.PID))
        } else {
            file.WriteString(fmt.Sprintf("    program_map_PID:%d\n", pmt.PID))
        }
    }
}

func (pat *Pat) Encode(bsw *codec.BitStreamWriter) {
    bsw.PutUint8(0x00, 8)
    loc := bsw.ByteOffset()
    bsw.PutUint8(pat.Section_syntax_indicator, 1)
    bsw.PutUint8(0x00, 1)
    bsw.PutUint8(0x03, 2)
    bsw.PutUint16(0, 12)
    bsw.Markdot()
    bsw.PutUint16(pat.Transport_stream_id, 16)
    bsw.PutUint8(0x03, 2)
    bsw.PutUint8(pat.Version_number, 5)
    bsw.PutUint8(pat.Current_next_indicator, 1)
    bsw.PutUint8(pat.Section_number, 8)
    bsw.PutUint8(pat.Last_section_number, 8)
    for _, pms := range pat.Pmts {
        bsw.PutUint16(pms.Program_number, 16)
        bsw.PutUint8(0x07, 3)
        bsw.PutUint16(pms.PID, 13)
    }
    length := bsw.DistanceFromMarkDot()
    //|Section_syntax_indicator|'0'|reserved|Section_length|
    pat.Section_length = uint16(length)/8 + 4
    bsw.SetUint16(pat.Section_length&0x0FFF|(uint16(pat.Section_syntax_indicator)<<15)|0x3000, loc)
    crc := codec.CalcCrc32(0xffffffff, bsw.Bits()[bsw.ByteOffset()-int(pat.Section_length-4)-3:bsw.ByteOffset()])
    tmpcrc := make([]byte, 4)
    binary.LittleEndian.PutUint32(tmpcrc, crc)
    bsw.PutBytes(tmpcrc)
}

func (pat *Pat) Decode(bs *codec.BitStream) error {
    pat.Table_id = bs.Uint8(8)

    if pat.Table_id != uint8(TS_TID_PAS) {
        return errors.New("table id is Not TS_TID_PAS")
    }
    pat.Section_syntax_indicator = bs.Uint8(1)
    bs.SkipBits(3)
    pat.Section_length = bs.Uint16(12)
    pat.Transport_stream_id = bs.Uint16(16)
    bs.SkipBits(2)
    pat.Version_number = bs.Uint8(5)
    pat.Current_next_indicator = bs.Uint8(1)
    pat.Section_number = bs.Uint8(8)
    pat.Last_section_number = bs.Uint8(8)
    for i := 0; i+4 <= int(pat.Section_length)-5-4; i = i + 4 {
        tmp := PmtPair{
            Program_number: 0,
            PID:            0,
        }
        tmp.Program_number = bs.Uint16(16)
        bs.SkipBits(3)
        tmp.PID = bs.Uint16(13)
        pat.Pmts = append(pat.Pmts, tmp)
    }
    return nil
}

type StreamPair struct {
    StreamType     uint8  //8 uimsbf
    Elementary_PID uint16 //13 uimsbf
    ES_Info_Length uint16 //12 uimsbf
}

type Pmt struct {
    Table_id                 uint8  //8  uimsbf
    Section_syntax_indicator uint8  //1  bslbf
    Section_length           uint16 //12 uimsbf
    Program_number           uint16 //16 uimsbf
    Version_number           uint8  //5  uimsbf
    Current_next_indicator   uint8  //1  bslbf
    Section_number           uint8  //8  uimsbf
    Last_section_number      uint8  //8  uimsbf
    PCR_PID                  uint16 //13 uimsbf
    Program_info_length      uint16 //12 uimsbf
    Streams                  []StreamPair
}

func NewPmt() *Pmt {
    return &Pmt{
        Table_id: uint8(TS_TID_PMS),
        Streams:  make([]StreamPair, 0, 8),
    }
}

func (pmt *Pmt) PrettyPrint(file *os.File) {
    file.WriteString(fmt.Sprintf("Table id:%d\n", pmt.Table_id))
    file.WriteString(fmt.Sprintf("Section_syntax_indicator:%d\n", pmt.Section_syntax_indicator))
    file.WriteString(fmt.Sprintf("Section_length:%d\n", pmt.Section_length))
    file.WriteString(fmt.Sprintf("Program_number:%d\n", pmt.Program_number))
    file.WriteString(fmt.Sprintf("Version_number:%d\n", pmt.Version_number))
    file.WriteString(fmt.Sprintf("Current_next_indicator:%d\n", pmt.Current_next_indicator))
    file.WriteString(fmt.Sprintf("Section_number:%d\n", pmt.Section_number))
    file.WriteString(fmt.Sprintf("Last_section_number:%d\n", pmt.Last_section_number))
    file.WriteString(fmt.Sprintf("PCR_PID:%d\n", pmt.PCR_PID))
    file.WriteString(fmt.Sprintf("program_info_length:%d\n", pmt.Program_info_length))
    for i, stream := range pmt.Streams {
        file.WriteString(fmt.Sprintf("----stream %d\n", i))
        if stream.StreamType == uint8(TS_STREAM_AAC) {
            file.WriteString("    stream_type:AAC\n")
        } else if stream.StreamType == uint8(TS_STREAM_H264) {
            file.WriteString("    stream_type:H264\n")
        } else if stream.StreamType == uint8(TS_STREAM_H265) {
            file.WriteString("    stream_type:H265\n")
        }
        file.WriteString(fmt.Sprintf("    elementary_PID:%d\n", stream.Elementary_PID))
        file.WriteString(fmt.Sprintf("    ES_info_length:%d\n", stream.ES_Info_Length))
    }
}

func (pmt *Pmt) Encode(bsw *codec.BitStreamWriter) {
    bsw.PutUint8(pmt.Table_id, 8)
    loc := bsw.ByteOffset()
    bsw.PutUint8(pmt.Section_syntax_indicator, 1)
    bsw.PutUint8(0x00, 1)
    bsw.PutUint8(0x03, 2)
    bsw.PutUint16(pmt.Section_length, 12)
    bsw.Markdot()
    bsw.PutUint16(pmt.Program_number, 16)
    bsw.PutUint8(0x03, 2)
    bsw.PutUint8(pmt.Version_number, 5)
    bsw.PutUint8(pmt.Current_next_indicator, 1)
    bsw.PutUint8(pmt.Section_number, 8)
    bsw.PutUint8(pmt.Last_section_number, 8)
    bsw.PutUint8(0x07, 3)
    bsw.PutUint16(pmt.PCR_PID, 13)
    bsw.PutUint8(0x0f, 4)
    //TODO Program info length
    bsw.PutUint16(0x0000 /*pmt.Program_info_length*/, 12)
    for _, stream := range pmt.Streams {
        bsw.PutUint8(stream.StreamType, 8)
        bsw.PutUint8(0x00, 3)
        bsw.PutUint16(stream.Elementary_PID, 13)
        bsw.PutUint8(0x00, 4)
        //TODO ES_info
        bsw.PutUint8(0 /*ES_info_length*/, 12)
    }
    length := bsw.DistanceFromMarkDot()
    pmt.Section_length = uint16(length)/8 + 4
    bsw.SetUint16(pmt.Section_length&0x0FFF|(uint16(pmt.Section_syntax_indicator)<<15)|0x3000, loc)
    crc := codec.CalcCrc32(0xffffffff, bsw.Bits()[bsw.ByteOffset()-int(pmt.Section_length-4)-3:bsw.ByteOffset()])
    tmpcrc := make([]byte, 4)
    binary.LittleEndian.PutUint32(tmpcrc, crc)
    bsw.PutBytes(tmpcrc)
}

func (pmt *Pmt) Decode(bs *codec.BitStream) error {
    pmt.Table_id = bs.Uint8(8)
    if pmt.Table_id != uint8(TS_TID_PMS) {
        return errors.New("table id is Not TS_TID_PAS")
    }
    pmt.Section_syntax_indicator = bs.Uint8(1)
    bs.SkipBits(3)
    pmt.Section_length = bs.Uint16(12)
    pmt.Program_number = bs.Uint16(16)
    bs.SkipBits(2)
    pmt.Version_number = bs.Uint8(5)
    pmt.Current_next_indicator = bs.Uint8(1)
    pmt.Section_number = bs.Uint8(8)
    pmt.Last_section_number = bs.Uint8(8)
    bs.SkipBits(3)
    pmt.PCR_PID = bs.Uint16(13)
    bs.SkipBits(4)
    pmt.Program_info_length = bs.Uint16(12)
    //TODO N loop descriptors
    bs.SkipBits(int(pmt.Program_info_length) * 8)
    //fmt.Printf("section length %d pmt.Pogram_info_length=%d\n", pmt.Section_length, pmt.Pogram_info_length)
    for i := 0; i < int(pmt.Section_length)-9-int(pmt.Program_info_length)-4; {
        tmp := StreamPair{
            StreamType:     0,
            Elementary_PID: 0,
            ES_Info_Length: 0,
        }
        tmp.StreamType = bs.Uint8(8)
        bs.SkipBits(3)
        tmp.Elementary_PID = bs.Uint16(13)
        bs.SkipBits(4)
        tmp.ES_Info_Length = bs.Uint16(12)
        //TODO N loop descriptors
        bs.SkipBits(int(tmp.ES_Info_Length) * 8)
        pmt.Streams = append(pmt.Streams, tmp)
        i += 5 + int(tmp.ES_Info_Length)
    }
    return nil
}
