package mpeg2

import (
    "errors"
    "io"

    "github.com/yapingcat/gomedia/codec"
)

type pakcet_t struct {
    payload []byte
    pts     uint64
    dts     uint64
}

func newPacket_t(size uint32) *pakcet_t {
    return &pakcet_t{
        payload: make([]byte, 0, size),
        pts:     0,
        dts:     0,
    }
}

type tsstream struct {
    cid     TS_STREAM_TYPE
    pes_sid PES_STREMA_ID
    pes_pkg *PesPacket
    pkg     *pakcet_t
}

type tsprogram struct {
    pn      uint16
    streams map[uint16]*tsstream
}

type TSDemuxer struct {
    programs   map[uint16]*tsprogram
    OnFrame    func(cid TS_STREAM_TYPE, frame []byte, pts uint64, dts uint64)
    OnTSPacket func(pkg *TSPacket)
}

func NewTSDemuxer() *TSDemuxer {
    return &TSDemuxer{
        programs:   make(map[uint16]*tsprogram),
        OnFrame:    nil,
        OnTSPacket: nil,
    }
}

func (demuxer *TSDemuxer) Input(r io.Reader) error {
    buf := make([]byte, TS_PAKCET_SIZE)
    _, err := io.ReadFull(r, buf)
    if err != nil {
        return errNeedMore
    }
    for {
        bs := codec.NewBitStream(buf)
        var pkg TSPacket
        if err := pkg.DecodeHeader(bs); err != nil {
            return err
        }
        if pkg.PID == uint16(TS_PID_PAT) {
            if pkg.Payload_unit_start_indicator == 1 {
                bs.SkipBits(8)
            }
            pat := NewPat()
            if err := pat.Decode(bs); err != nil {
                return err
            }
            pkg.Payload = pat
            if pat.Table_id != uint8(TS_TID_PAS) {
                return errors.New("pat table id is wrong")
            }
            for _, pmt := range pat.Pmts {
                if pmt.Program_number != 0x0000 {
                    if _, found := demuxer.programs[pmt.PID]; !found {
                        demuxer.programs[pmt.PID] = &tsprogram{pn: 0, streams: make(map[uint16]*tsstream)}
                    }
                }
            }
        } else {
            for p, s := range demuxer.programs {
                if p == pkg.PID { // pmt table
                    if pkg.Payload_unit_start_indicator == 1 {
                        bs.SkipBits(8) //pointer filed
                    }
                    pmt := NewPmt()
                    if err := pmt.Decode(bs); err != nil {
                        return err
                    }
                    pkg.Payload = pmt
                    s.pn = pmt.Program_number
                    for _, ps := range pmt.Streams {
                        if _, found := s.streams[ps.Elementary_PID]; !found {
                            s.streams[ps.Elementary_PID] = &tsstream{
                                cid:     TS_STREAM_TYPE(ps.StreamType),
                                pes_sid: findPESIDByStreamType(TS_STREAM_TYPE(ps.StreamType)),
                                pes_pkg: NewPesPacket(),
                            }
                        }
                    }
                } else {
                    for sid, stream := range s.streams {
                        if sid != pkg.PID {
                            continue
                        }
                        if pkg.Payload_unit_start_indicator == 1 {
                            err := stream.pes_pkg.Decode(bs)
                            // ignore error if it was a short payload read, next ts packet should append missing data
                            if err != nil && !(errors.Is(err, errNeedMore) && stream.pes_pkg.Pes_payload != nil) {
                                return err
                            }
                            pkg.Payload = stream.pes_pkg
                        } else {
                            stream.pes_pkg.Pes_payload = bs.RemainData()
                            pkg.Payload = bs.RemainData()
                        }
                        stype := findPESIDByStreamType(stream.cid)
                        if stype == PES_STREAM_AUDIO {
                            demuxer.doAudioPesPacket(stream, pkg.Payload_unit_start_indicator)
                        } else if stype == PES_STREAM_VIDEO {
                            demuxer.doVideoPesPacket(stream, pkg.Payload_unit_start_indicator)
                        }
                    }
                }
            }
        }
        if demuxer.OnTSPacket != nil {
            demuxer.OnTSPacket(&pkg)
        }
        _, err := io.ReadFull(r, buf)
        if err != nil {
            if errors.Is(err, io.EOF) {
                break
            } else {
                return errNeedMore
            }
        }
    }
    demuxer.flush()
    return nil
}

func (demuxer *TSDemuxer) flush() {
    for _, pm := range demuxer.programs {
        for _, stream := range pm.streams {
            if stream.pkg == nil || len(stream.pkg.payload) == 0 {
                continue
            }
            if demuxer.OnFrame != nil {
                demuxer.OnFrame(stream.cid, stream.pkg.payload, stream.pkg.pts/90, stream.pkg.dts/90)
            }
        }
    }
}

func (demuxer *TSDemuxer) doVideoPesPacket(stream *tsstream, start uint8) {
    if stream.cid != TS_STREAM_H264 && stream.cid != TS_STREAM_H265 {
        return
    }
    if stream.pkg == nil {
        stream.pkg = newPacket_t(1024)
        stream.pkg.pts = stream.pes_pkg.Pts
        stream.pkg.dts = stream.pes_pkg.Dts
    }
    stream.pkg.payload = append(stream.pkg.payload, stream.pes_pkg.Pes_payload...)
    demuxer.splitH26XFrame(stream)
    stream.pkg.pts = stream.pes_pkg.Pts
    stream.pkg.dts = stream.pes_pkg.Dts
}

func (demuxer *TSDemuxer) doAudioPesPacket(stream *tsstream, start uint8) {
    if stream.cid != TS_STREAM_AAC {
        return
    }

    if stream.pkg == nil {
        stream.pkg = newPacket_t(1024)
        stream.pkg.pts = stream.pes_pkg.Pts
        stream.pkg.dts = stream.pes_pkg.Dts
    }

    if len(stream.pkg.payload) > 0 && (start == 1 || stream.pes_pkg.Pts != stream.pkg.pts) {
        if demuxer.OnFrame != nil {
            demuxer.OnFrame(stream.cid, stream.pkg.payload, stream.pkg.pts/90, stream.pkg.dts/90)
        }
        stream.pkg.payload = stream.pkg.payload[:0]
    }
    stream.pkg.payload = append(stream.pkg.payload, stream.pes_pkg.Pes_payload...)
    stream.pkg.pts = stream.pes_pkg.Pts
    stream.pkg.dts = stream.pes_pkg.Dts
}

func (demuxer *TSDemuxer) splitH26XFrame(stream *tsstream) {
    data := stream.pkg.payload
    start, _ := codec.FindStartCode(data, 0)
    datalen := len(data)
    for start < datalen {
        end, _ := codec.FindStartCode(data, start+3)
        if end < 0 {
            break
        }
        if (stream.cid == TS_STREAM_H264 && codec.H264NaluTypeWithoutStartCode(data[start:end]) == codec.H264_NAL_AUD) ||
            (stream.cid == TS_STREAM_H265 && codec.H265NaluTypeWithoutStartCode(data[start:end]) == codec.H265_NAL_AUD) {
            start = end
            continue
        }
        if demuxer.OnFrame != nil {
            demuxer.OnFrame(stream.cid, data[start:end], stream.pkg.pts/90, stream.pkg.dts/90)
        }
        start = end
    }
    if start == 0 {
        return
    }
    copy(stream.pkg.payload, data[start:datalen])
    stream.pkg.payload = stream.pkg.payload[0 : datalen-start]
}
