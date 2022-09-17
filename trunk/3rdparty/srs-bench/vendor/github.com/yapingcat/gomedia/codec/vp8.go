package codec

import "errors"

type VP8FrameTag struct {
    FrameType     uint32 //0: I frame , 1: P frame
    Version       uint32
    Display       uint32
    FirstPartSize uint32
}

type VP8KeyFrameHead struct {
    Width      int
    Height     int
    HorizScale int
    VertScale  int
}

func DecodeFrameTag(frame []byte) (*VP8FrameTag, error) {
    if len(frame) < 3 {
        return nil, errors.New("frame bytes < 3")
    }
    var tmp uint32 = (uint32(frame[2]) << 16) | (uint32(frame[1]) << 8) | uint32(frame[0])
    tag := &VP8FrameTag{}
    tag.FrameType = tmp & 0x01
    tag.Version = (tmp >> 1) & 0x07
    tag.Display = (tmp >> 4) & 0x01
    tag.FirstPartSize = (tmp >> 5) & 0x7FFFF
    return tag, nil
}

func DecodeKeyFrameHead(frame []byte) (*VP8KeyFrameHead, error) {
    if len(frame) < 7 {
        return nil, errors.New("frame bytes < 3")
    }

    if frame[0] != 0x9d || frame[1] != 0x01 || frame[2] != 0x2a {
        return nil, errors.New("not find Start code")
    }

    head := &VP8KeyFrameHead{}
    head.Width = int(uint16(frame[4]&0x3f)<<8 | uint16(frame[3]))
    head.HorizScale = int(frame[4] >> 6)
    head.Height = int(uint16(frame[6]&0x3f)<<8 | uint16(frame[5]))
    head.VertScale = int(frame[6] >> 6)
    return head, nil
}

func IsKeyFrame(frame []byte) bool {
    tag, err := DecodeFrameTag(frame)
    if err != nil {
        return false
    }

    if tag.FrameType == 0 {
        return true
    } else {
        return false
    }
}

func GetResloution(frame []byte) (width int, height int, err error) {
    if !IsKeyFrame(frame) {
        return 0, 0, errors.New("the frame is not Key frame")
    }

    head, err := DecodeKeyFrameHead(frame[3:])
    if err != nil {
        return 0, 0, err
    }
    return head.Width, head.Height, nil
}
