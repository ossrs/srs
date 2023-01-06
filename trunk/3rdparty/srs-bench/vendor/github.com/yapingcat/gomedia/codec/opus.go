package codec

import (
    "encoding/binary"
    "errors"
)

// rfc6716 https://datatracker.ietf.org/doc/html/rfc6716
//
// TOC byte
// 0
// 0 1 2 3 4 5 6 7
// +-+-+-+-+-+-+-+-+
// | config  |s| c |
// +-+-+-+-+-+-+-+-+

// +-----------------------+-----------+-----------+-------------------+
// | Configuration         | Mode      | Bandwidth | Frame Sizes       |
// | Number(s)             |           |           |                   |
// +-----------------------+-----------+-----------+-------------------+
// | 0...3                 | SILK-only | NB        | 10, 20, 40, 60 ms |
// |                       |           |           |                   |
// | 4...7                 | SILK-only | MB        | 10, 20, 40, 60 ms |
// |                       |           |           |                   |
// | 8...11                | SILK-only | WB        | 10, 20, 40, 60 ms |
// |                       |           |           |                   |
// | 12...13               | Hybrid    | SWB       | 10, 20 ms         |
// |                       |           |           |                   |
// | 14...15               | Hybrid    | FB        | 10, 20 ms         |
// |                       |           |           |                   |
// | 16...19               | CELT-only | NB        | 2.5, 5, 10, 20 ms |
// |                       |           |           |                   |
// | 20...23               | CELT-only | WB        | 2.5, 5, 10, 20 ms |
// |                       |           |           |                   |
// | 24...27               | CELT-only | SWB       | 2.5, 5, 10, 20 ms |
// |                       |           |           |                   |
// | 28...31               | CELT-only | FB        | 2.5, 5, 10, 20 ms |
// +-----------------------+-----------+-----------+-------------------+

// s: with 0 indicating mono and 1 indicating stereo.
//
// c : codes 0 to 3
// 0: 1 frame in the packet
// 1: 2 frames in the packet, each with equal compressed size
// 2: 2 frames in the packet, with different compressed sizes
// 3: an arbitrary number of frames in the packet

// Frame Length Coding
// 0: No frame (Discontinuous Transmission (DTX) or lost packet)
// 1...251: Length of the frame in bytes
// 252...255: A second byte is needed.  The total length is (second_byte*4)+first_byte

// Code 0:
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | config  |s|0|0|                                               |
// +-+-+-+-+-+-+-+-+                                               |
// |                    Compressed frame 1 (N-1 bytes)...          :
// :                                                               |
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// Code 1:
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | config  |s|0|1|                                               |
// +-+-+-+-+-+-+-+-+                                               :
// |             Compressed frame 1 ((N-1)/2 bytes)...             |
// :                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                               |                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               :
// |             Compressed frame 2 ((N-1)/2 bytes)...             |
// :                                               +-+-+-+-+-+-+-+-+
// |                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// Code 2:
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | config  |s|1|0| N1 (1-2 bytes):                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               :
// |               Compressed frame 1 (N1 bytes)...                |
// :                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                               |                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               |
// |                     Compressed frame 2...                     :
// :                                                               |
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

//Code 3:
//  Frame Count Byte
//  0
//  0 1 2 3 4 5 6 7
// +-+-+-+-+-+-+-+-+
// |v|p|     M     |
// +-+-+-+-+-+-+-+-+
// v: 0 - CBR 1 - VBR
// p: 0 - no padding 1 - padding after frame
// M: frame count

// A CBR Code 3 Packet
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | config  |s|1|1|0|p|     M     |  Padding length (Optional)    :
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                                                               |
// :               Compressed frame 1 (R/M bytes)...               :
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                                                               |
// :               Compressed frame 2 (R/M bytes)...               :
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                                                               |
// :                              ...                              :
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                                                               |
// :               Compressed frame M (R/M bytes)...               :
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// :                  Opus Padding (Optional)...                   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//

//
//  A VBR Code 3 Packet
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | config  |s|1|1|1|p|     M     | Padding length (Optional)     :
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// : N1 (1-2 bytes): N2 (1-2 bytes):     ...       :     N[M-1]    |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                                                               |
// :               Compressed frame 1 (N1 bytes)...                :
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                                                               |
// :               Compressed frame 2 (N2 bytes)...                :
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                                                               |
// :                              ...                              :
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                                                               |
// :                     Compressed frame M...                     :
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// :                  Opus Padding (Optional)...                   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

var (
    /// 10ms,20ms,40ms,60ms, samplerate 48000
    //  sample num per millisecond
    //  48000 / 1000ms * 10 = 480 ...
    SLKOpusSampleSize    [4]int = [4]int{480, 960, 1920, 2880}
    HybridOpusSampleSize [4]int = [4]int{480, 960}
    CELTOpusSampleSize   [4]int = [4]int{120, 210, 480, 960}
)

func OpusPacketDuration(packet []byte) uint64 {
    config := int(packet[0] >> 3)
    code := packet[0] & 0x03
    frameCount := 0
    var duration uint64
    if code == 0 {
        frameCount = 1
    } else if code == 1 || code == 2 {
        frameCount = 2
    } else if code == 3 {
        frameCount = int(packet[1] & 0x1F)
    } else {
        panic("code must <= 3")
    }

    switch {
    case config >= 0 && config < 12:
        duration = uint64(frameCount * SLKOpusSampleSize[config%4])
    case config >= 12 && config < 16:
        duration = uint64(frameCount * HybridOpusSampleSize[config%2])
    case config >= 16 && config < 32:
        duration = uint64(frameCount * CELTOpusSampleSize[config%4])
    default:
        panic("unkown opus config")
    }

    return duration
}

//ffmpeg opus.h OpusPacket
type OpusPacket struct {
    Code       int
    Config     int
    Stereo     int
    Vbr        int
    FrameCount int
    FrameLen   []uint16
    Frame      []byte
    Duration   uint64
}

func DecodeOpusPacket(packet []byte) *OpusPacket {
    pkt := &OpusPacket{}
    pkt.Code = int(packet[0] & 0x03)
    pkt.Stereo = int((packet[0] >> 2) & 0x01)
    pkt.Config = int(packet[0] >> 3)

    switch pkt.Code {
    case 0:
        pkt.FrameCount = 1
        pkt.FrameLen = make([]uint16, 1)
        pkt.FrameLen[0] = uint16(len(packet) - 1)
        pkt.Frame = packet[1:]
    case 1:
        pkt.FrameCount = 2
        pkt.FrameLen = make([]uint16, 1)
        pkt.FrameLen[0] = uint16(len(packet)-1) / 2
        pkt.Frame = packet[1:]
    case 2:
        pkt.FrameCount = 2
        hdr := 1
        N1 := int(packet[1])
        if N1 >= 252 {
            N1 = N1 + int(packet[2]*4)
            hdr = 2
        }
        pkt.FrameLen = make([]uint16, 2)
        pkt.FrameLen[0] = uint16(N1)
        pkt.FrameLen[1] = uint16(len(packet)-hdr) - uint16(N1)
    case 3:
        hdr := 2
        pkt.Vbr = int(packet[1] >> 7)
        padding := packet[1] >> 6
        pkt.FrameCount = int(packet[1] & 0x1F)
        paddingLen := 0
        if padding == 1 {
            for packet[hdr] == 255 {
                paddingLen += 254
                hdr++
            }
            paddingLen += int(packet[hdr])
        }

        if pkt.Vbr == 0 {
            pkt.FrameLen = make([]uint16, 1)
            pkt.FrameLen[0] = uint16(len(packet)-hdr-paddingLen) / uint16(pkt.FrameCount)
            pkt.Frame = packet[hdr : hdr+int(pkt.FrameLen[0]*uint16(pkt.FrameCount))]
        } else {
            n := 0
            for i := 0; i < int(pkt.FrameCount)-1; i++ {
                N1 := int(packet[hdr])
                hdr += 1
                if N1 >= 252 {
                    N1 = N1 + int(packet[hdr]*4)
                    hdr += 1
                }
                n += N1
                pkt.FrameLen = append(pkt.FrameLen, uint16(N1))
            }
            lastFrameLen := len(packet) - hdr - paddingLen - n
            pkt.FrameLen = append(pkt.FrameLen, uint16(lastFrameLen))
            pkt.Frame = packet[hdr : hdr+n+lastFrameLen]
        }
    default:
        panic("Error C must <= 3")
    }
    OpusPacketDuration(packet)
    return pkt
}

const (
    LEFT_CHANNEL  = 0
    RIGHT_CHANNEL = 1
)

var (
    vorbisChanLayoutOffset [8][8]byte = [8][8]byte{
        {0},
        {0, 1},
        {0, 2, 1},
        {0, 1, 2, 3},
        {0, 2, 1, 3, 4},
        {0, 2, 1, 5, 3, 4},
        {0, 2, 1, 6, 5, 3, 4},
        {0, 2, 1, 7, 5, 6, 3, 4},
    }
)

type ChannelOrder func(channels int, idx int) int

func defalutOrder(channels int, idx int) int {
    return idx
}

func vorbisOrder(channels int, idx int) int {
    return int(vorbisChanLayoutOffset[channels-1][idx])
}

type ChannelMap struct {
    StreamIdx  int
    ChannelIdx int
    Silence    bool
    Copy       bool
    CopyFrom   int
}

type OpusContext struct {
    Preskip           int
    SampleRate        int
    ChannelCount      int
    StreamCount       int
    StereoStreamCount int
    OutputGain        uint16
    MapType           uint8
    ChannelMaps       []ChannelMap
}

// opus ID Head
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |      'O'      |      'p'      |      'u'      |      's'      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |      'H'      |      'e'      |      'a'      |      'd'      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  Version = 1  | Channel Count |           Pre-skip            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                     Input Sample Rate (Hz)                    |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |   Output Gain (Q7.8 in dB)    | Mapping Family|               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+               :
// |                                                               |
// :               Optional Channel Mapping Table...               :
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//                                                 +-+-+-+-+-+-+-+-+
//                                                 | Stream Count  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | Coupled Count |              Channel Mapping...               :
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
func (ctx *OpusContext) ParseExtranData(extraData []byte) error {
    if string(extraData[0:8]) != "OpusHead" {
        return errors.New("magic signature must equal OpusHead")
    }

    _ = extraData[8] // version
    ctx.ChannelCount = int(extraData[9])
    ctx.Preskip = int(binary.LittleEndian.Uint16(extraData[10:]))
    ctx.SampleRate = int(binary.LittleEndian.Uint32(extraData[12:]))
    ctx.OutputGain = binary.LittleEndian.Uint16(extraData[16:])
    ctx.MapType = extraData[18]
    var channel []byte
    var order ChannelOrder
    if ctx.MapType == 0 {
        ctx.StreamCount = 1
        ctx.StereoStreamCount = ctx.ChannelCount - 1
        channel = []byte{0, 1}
        order = defalutOrder
    } else if ctx.MapType == 1 || ctx.MapType == 2 || ctx.MapType == 255 {
        ctx.StreamCount = int(extraData[19])
        ctx.StereoStreamCount = int(extraData[20])
        if ctx.MapType == 1 {
            channel = extraData[21 : 21+ctx.ChannelCount]
            order = vorbisOrder
        }
    } else {
        return errors.New("unsupport map type 255")
    }

    for i := 0; i < ctx.ChannelCount; i++ {
        cm := ChannelMap{}
        index := channel[order(ctx.ChannelCount, i)]
        if index == 255 {
            cm.Silence = true
            continue
        } else if index > byte(ctx.StereoStreamCount)+byte(ctx.StreamCount) {
            return errors.New("index must < (streamcount + stereo streamcount)")
        }

        for j := 0; j < i; j++ {
            if channel[order(ctx.ChannelCount, i)] == index {
                cm.Copy = true
                cm.CopyFrom = j
                break
            }
        }

        if int(index) < 2*ctx.StereoStreamCount {
            cm.StreamIdx = int(index) / 2
            if index&1 == 0 {
                cm.ChannelIdx = LEFT_CHANNEL
            } else {
                cm.ChannelIdx = RIGHT_CHANNEL
            }
        } else {
            cm.StreamIdx = int(index) - ctx.StereoStreamCount
            cm.ChannelIdx = 0
        }
        ctx.ChannelMaps = append(ctx.ChannelMaps, cm)
    }

    return nil
}

func (ctx *OpusContext) WriteOpusExtraData() []byte {
    extraData := make([]byte, 19)
    copy(extraData, string("OpusHead"))
    extraData[8] = 0x01
    extraData[9] = byte(ctx.ChannelCount)
    binary.LittleEndian.PutUint16(extraData[10:], uint16(ctx.Preskip))
    binary.LittleEndian.PutUint32(extraData[12:], uint32(ctx.SampleRate))
    return extraData
}

func WriteDefaultOpusExtraData() []byte {
    return []byte{
        'O', 'p', 'u', 's', 'H', 'e', 'a', 'd',
        1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    }
}
