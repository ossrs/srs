package codec

import "errors"

// Table 31 – Profiles
// index      profile
//   0        Main profile
//   1        Low Complexity profile (LC)
//   2        Scalable Sampling Rate profile (SSR)
//   3        (reserved)

type AAC_PROFILE int

const (
    MAIN AAC_PROFILE = iota
    LC
    SSR
)

type AAC_SAMPLING_FREQUENCY int

const (
    AAC_SAMPLE_96000 AAC_SAMPLING_FREQUENCY = iota
    AAC_SAMPLE_88200
    AAC_SAMPLE_64000
    AAC_SAMPLE_48000
    AAC_SAMPLE_44100
    AAC_SAMPLE_32000
    AAC_SAMPLE_24000
    AAC_SAMPLE_22050
    AAC_SAMPLE_16000
    AAC_SAMPLE_11025
    AAC_SAMPLE_8000
)

var AAC_Sampling_Idx [11]int = [11]int{96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 11025, 8000}

// Table 4 – Syntax of adts_sequence()
// adts_sequence() {
//         while (nextbits() == syncword) {
//             adts_frame();
//         }
// }
// Table 5 – Syntax of adts_frame()
// adts_frame() {
//     adts_fixed_header();
//     adts_variable_header();
//     if (number_of_raw_data_blocks_in_frame == 0) {
//         adts_error_check();
//         raw_data_block();
//     }
//     else {
//         adts_header_error_check();
//         for (i = 0; i <= number_of_raw_data_blocks_in_frame;i++ {
//             raw_data_block();
//             adts_raw_data_block_error_check();
//         }
//     }
// }

// adts_fixed_header()
// {
//         syncword;                         12           bslbf
//         ID;                                1            bslbf
//         layer;                          2            uimsbf
//         protection_absent;              1            bslbf
//         profile;                        2            uimsbf
//         sampling_frequency_index;       4            uimsbf
//         private_bit;                    1            bslbf
//         channel_configuration;          3            uimsbf
//         original/copy;                  1            bslbf
//         home;                           1            bslbf
// }

type ADTS_Fix_Header struct {
    ID                       uint8
    Layer                    uint8
    Protection_absent        uint8
    Profile                  uint8
    Sampling_frequency_index uint8
    Private_bit              uint8
    Channel_configuration    uint8
    Originalorcopy           uint8
    Home                     uint8
}

// adts_variable_header() {
//      copyright_identification_bit;               1      bslbf
//      copyright_identification_start;             1      bslbf
//      frame_length;                               13     bslbf
//      adts_buffer_fullness;                       11     bslbf
//      number_of_raw_data_blocks_in_frame;         2      uimsfb
// }

type ADTS_Variable_Header struct {
    Copyright_identification_bit       uint8
    copyright_identification_start     uint8
    Frame_length                       uint16
    Adts_buffer_fullness               uint16
    Number_of_raw_data_blocks_in_frame uint8
}

type ADTS_Frame_Header struct {
    Fix_Header      ADTS_Fix_Header
    Variable_Header ADTS_Variable_Header
}

func NewAdtsFrameHeader() *ADTS_Frame_Header {
    return &ADTS_Frame_Header{
        Fix_Header: ADTS_Fix_Header{
            ID:                       0,
            Layer:                    0,
            Protection_absent:        1,
            Profile:                  uint8(MAIN),
            Sampling_frequency_index: uint8(AAC_SAMPLE_44100),
            Private_bit:              0,
            Channel_configuration:    0,
            Originalorcopy:           0,
            Home:                     0,
        },

        Variable_Header: ADTS_Variable_Header{
            copyright_identification_start:     0,
            Copyright_identification_bit:       0,
            Frame_length:                       0,
            Adts_buffer_fullness:               0,
            Number_of_raw_data_blocks_in_frame: 0,
        },
    }
}

func (frame *ADTS_Frame_Header) Decode(aac []byte) error {
    _ = aac[6]
    frame.Fix_Header.ID = aac[1] >> 3
    frame.Fix_Header.Layer = aac[1] >> 1 & 0x03
    frame.Fix_Header.Protection_absent = aac[1] & 0x01
    frame.Fix_Header.Profile = aac[2] >> 6 & 0x03
    frame.Fix_Header.Sampling_frequency_index = aac[2] >> 2 & 0x0F
    frame.Fix_Header.Private_bit = aac[2] >> 1 & 0x01
    frame.Fix_Header.Channel_configuration = (aac[2] & 0x01 << 2) | (aac[3] >> 6)
    frame.Fix_Header.Originalorcopy = aac[3] >> 5 & 0x01
    frame.Fix_Header.Home = aac[3] >> 4 & 0x01
    frame.Variable_Header.Copyright_identification_bit = aac[3] >> 3 & 0x01
    frame.Variable_Header.copyright_identification_start = aac[3] >> 2 & 0x01
    frame.Variable_Header.Frame_length = (uint16(aac[3]&0x03) << 11) | (uint16(aac[4]) << 3) | (uint16(aac[5]>>5) & 0x07)
    frame.Variable_Header.Adts_buffer_fullness = (uint16(aac[5]&0x1F) << 6) | uint16(aac[6]>>2)
    frame.Variable_Header.Number_of_raw_data_blocks_in_frame = aac[6] & 0x03
    return nil
}

func (frame *ADTS_Frame_Header) Encode() []byte {
    var hdr []byte
    if frame.Fix_Header.Protection_absent == 1 {
        hdr = make([]byte, 7)
    } else {
        hdr = make([]byte, 9)
    }
    hdr[0] = 0xFF
    hdr[1] = 0xF0
    hdr[1] = hdr[1] | (frame.Fix_Header.ID << 3) | (frame.Fix_Header.Layer << 1) | frame.Fix_Header.Protection_absent
    hdr[2] = frame.Fix_Header.Profile<<6 | frame.Fix_Header.Sampling_frequency_index<<2 | frame.Fix_Header.Private_bit<<1 | frame.Fix_Header.Channel_configuration>>2
    hdr[3] = frame.Fix_Header.Channel_configuration<<6 | frame.Fix_Header.Originalorcopy<<5 | frame.Fix_Header.Home<<4
    hdr[3] = hdr[3] | frame.Variable_Header.copyright_identification_start<<3 | frame.Variable_Header.Copyright_identification_bit<<2 | byte(frame.Variable_Header.Frame_length<<11)
    hdr[4] = byte(frame.Variable_Header.Frame_length >> 3)
    hdr[5] = byte((frame.Variable_Header.Frame_length&0x07)<<5) | byte(frame.Variable_Header.Adts_buffer_fullness>>3)
    hdr[6] = byte(frame.Variable_Header.Adts_buffer_fullness&0x3F<<2) | frame.Variable_Header.Number_of_raw_data_blocks_in_frame
    return hdr
}

func SampleToAACSampleIndex(sampling int) int {
    for i, v := range AAC_Sampling_Idx {
        if v == sampling {
            return i
        }
    }
    panic("not Found AAC Sample Index")
}

func AACSampleIdxToSample(idx int) int {
    return AAC_Sampling_Idx[idx]
}

// +--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
// |  audio object type(5 bits)  |  sampling frequency index(4 bits) |   channel configuration(4 bits)  | GA framelength flag(1 bits) |  GA Depends on core coder(1 bits) | GA Extension Flag(1 bits) |
// +--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

type AudioSpecificConfiguration struct {
    Audio_object_type        uint8
    Sample_freq_index        uint8
    Channel_configuration    uint8
    GA_framelength_flag      uint8
    GA_depends_on_core_coder uint8
    GA_extension_flag        uint8
}

func NewAudioSpecificConfiguration() *AudioSpecificConfiguration {
    return &AudioSpecificConfiguration{
        Audio_object_type:        0,
        Sample_freq_index:        0,
        Channel_configuration:    0,
        GA_framelength_flag:      0,
        GA_depends_on_core_coder: 0,
        GA_extension_flag:        0,
    }
}

func (asc *AudioSpecificConfiguration) Encode() []byte {
    buf := make([]byte, 2)
    buf[0] = (asc.Audio_object_type & 0x1f << 3) | (asc.Sample_freq_index & 0x0F >> 1)
    buf[1] = (asc.Sample_freq_index & 0x0F << 7) | (asc.Channel_configuration & 0x0F << 3) | (asc.GA_framelength_flag & 0x01 << 2) | (asc.GA_depends_on_core_coder & 0x01 << 1) | (asc.GA_extension_flag & 0x01)
    return buf
}

func (asc *AudioSpecificConfiguration) Decode(buf []byte) error {

    if len(buf) < 2 {
        return errors.New("len of buf < 2 ")
    }

    asc.Audio_object_type = buf[0] >> 3
    asc.Sample_freq_index = (buf[0] & 0x07 << 1) | (buf[1] >> 7)
    asc.Channel_configuration = buf[1] >> 3 & 0x0F
    asc.GA_framelength_flag = buf[1] >> 2 & 0x01
    asc.GA_depends_on_core_coder = buf[1] >> 1 & 0x01
    asc.GA_extension_flag = buf[1] & 0x01
    return nil
}

func ConvertADTSToASC(frame []byte) ([]byte, error) {

    if len(frame) < 7 {
        return nil, errors.New("len of frame < 7")
    }

    adts := NewAdtsFrameHeader()
    adts.Decode(frame)
    asc := NewAudioSpecificConfiguration()
    asc.Audio_object_type = adts.Fix_Header.Profile + 1
    asc.Channel_configuration = adts.Fix_Header.Channel_configuration
    asc.Sample_freq_index = adts.Fix_Header.Sampling_frequency_index
    return asc.Encode(), nil
}

func ConvertASCToADTS(asc []byte, aacbytes int) []byte {
    aac_asc := NewAudioSpecificConfiguration()
    aac_asc.Decode(asc)
    aac_adts := NewAdtsFrameHeader()
    aac_adts.Fix_Header.Profile = aac_asc.Audio_object_type - 1
    aac_adts.Fix_Header.Channel_configuration = aac_asc.Channel_configuration
    aac_adts.Fix_Header.Sampling_frequency_index = aac_asc.Sample_freq_index
    aac_adts.Fix_Header.Protection_absent = 1
    aac_adts.Variable_Header.Adts_buffer_fullness = 0x3F
    aac_adts.Variable_Header.Frame_length = uint16(aacbytes)
    return aac_adts.Encode()
}
