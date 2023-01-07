package mpeg2

import "github.com/yapingcat/gomedia/codec"

type PSMuxer struct {
    system     *System_header
    psm        *Program_stream_map
    OnPacket   func(pkg []byte)
    firstframe bool
}

func NewPsMuxer() *PSMuxer {
    muxer := new(PSMuxer)
    muxer.firstframe = true
    muxer.system = new(System_header)
    muxer.system.Rate_bound = 26234
    muxer.psm = new(Program_stream_map)
    muxer.psm.Current_next_indicator = 1
    muxer.psm.Program_stream_map_version = 1
    muxer.OnPacket = nil
    return muxer
}

func (muxer *PSMuxer) AddStream(cid PS_STREAM_TYPE) uint8 {
    if cid == PS_STREAM_H265 || cid == PS_STREAM_H264 {
        es := NewElementary_Stream(uint8(PES_STREAM_VIDEO) + muxer.system.Video_bound)
        es.P_STD_buffer_bound_scale = 1
        es.P_STD_buffer_size_bound = 400
        muxer.system.Streams = append(muxer.system.Streams, es)
        muxer.system.Video_bound++
        muxer.psm.Stream_map = append(muxer.psm.Stream_map, NewElementary_stream_elem(uint8(cid), es.Stream_id))
        muxer.psm.Program_stream_map_version++
        return es.Stream_id
    } else {
        es := NewElementary_Stream(uint8(PES_STREAM_AUDIO) + muxer.system.Audio_bound)
        es.P_STD_buffer_bound_scale = 0
        es.P_STD_buffer_size_bound = 32
        muxer.system.Streams = append(muxer.system.Streams, es)
        muxer.system.Audio_bound++
        muxer.psm.Stream_map = append(muxer.psm.Stream_map, NewElementary_stream_elem(uint8(cid), es.Stream_id))
        muxer.psm.Program_stream_map_version++
        return es.Stream_id
    }
}

func (muxer *PSMuxer) Write(sid uint8, frame []byte, pts uint64, dts uint64) error {
    var stream *Elementary_stream_elem = nil
    for _, es := range muxer.psm.Stream_map {
        if es.Elementary_stream_id == sid {
            stream = es
            break
        }
    }
    if stream == nil {
        return errNotFound
    }
    var withaud bool = false
    var idr_flag bool = false
    var first bool = true
    var vcl bool = false
    if stream.Stream_type == uint8(PS_STREAM_H264) || stream.Stream_type == uint8(PS_STREAM_H265) {
        codec.SplitFrame(frame, func(nalu []byte) bool {
            if stream.Stream_type == uint8(PS_STREAM_H264) {
                nalu_type := codec.H264NaluTypeWithoutStartCode(nalu)
                if nalu_type == codec.H264_NAL_AUD {
                    withaud = true
                    return false
                } else if codec.IsH264VCLNaluType(nalu_type) {
                    if nalu_type == codec.H264_NAL_I_SLICE {
                        idr_flag = true
                    }
                    vcl = true
                    return false
                }
                return true
            } else {
                nalu_type := codec.H265NaluTypeWithoutStartCode(nalu)
                if nalu_type == codec.H265_NAL_AUD {
                    withaud = true
                    return false
                } else if codec.IsH265VCLNaluType(nalu_type) {
                    if nalu_type >= codec.H265_NAL_SLICE_BLA_W_LP && nalu_type <= codec.H265_NAL_SLICE_CRA {
                        idr_flag = true
                    }
                    vcl = true
                    return false
                }
                return true
            }
        })
    }

    dts = dts * 90
    pts = pts * 90
    bsw := codec.NewBitStreamWriter(1024)
    var pack PSPackHeader
    pack.System_clock_reference_base = dts - 3600
    pack.System_clock_reference_extension = 0
    pack.Program_mux_rate = 6106
    pack.Encode(bsw)
    if muxer.firstframe || idr_flag {
        muxer.system.Encode(bsw)
        muxer.psm.Encode(bsw)
        muxer.firstframe = false
    }
    if muxer.OnPacket != nil {
        muxer.OnPacket(bsw.Bits())
    }
    bsw.Reset()
    pespkg := NewPesPacket()
    for len(frame) > 0 {
        peshdrlen := 13
        pespkg.Stream_id = sid
        pespkg.PTS_DTS_flags = 0x03
        pespkg.PES_header_data_length = 10
        pespkg.Pts = pts
        pespkg.Dts = dts
        if idr_flag {
            pespkg.Data_alignment_indicator = 1
        }
        if first && !withaud && vcl {
            if stream.Stream_type == uint8(PS_STREAM_H264) {
                pespkg.Pes_payload = append(pespkg.Pes_payload, H264_AUD_NALU...)
                peshdrlen += 6
            } else if stream.Stream_type == uint8(PS_STREAM_H265) {
                pespkg.Pes_payload = append(pespkg.Pes_payload, H265_AUD_NALU...)
                peshdrlen += 7
            }
        }
        if peshdrlen+len(frame) >= 0xFFFF {
            pespkg.PES_packet_length = 0xFFFF
            pespkg.Pes_payload = append(pespkg.Pes_payload, frame[0:0xFFFF-peshdrlen]...)
            frame = frame[0xFFFF-peshdrlen:]
        } else {
            pespkg.PES_packet_length = uint16(peshdrlen + len(frame))
            pespkg.Pes_payload = append(pespkg.Pes_payload, frame[0:]...)
            frame = frame[:0]
        }
        pespkg.Encode(bsw)
        pespkg.Pes_payload = pespkg.Pes_payload[:0]
        if muxer.OnPacket != nil {
            muxer.OnPacket(bsw.Bits())
        }
        bsw.Reset()
        first = false
    }
    return nil
}
