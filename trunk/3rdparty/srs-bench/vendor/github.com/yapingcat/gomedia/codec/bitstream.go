package codec

import (
    "encoding/binary"
)

var BitMask [8]byte = [8]byte{0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF}

type BitStream struct {
    bits        []byte
    bytesOffset int
    bitsOffset  int
    bitsmark    int
    bytemark    int
}

func NewBitStream(buf []byte) *BitStream {
    return &BitStream{
        bits:        buf,
        bytesOffset: 0,
        bitsOffset:  0,
        bitsmark:    0,
        bytemark:    0,
    }
}

func (bs *BitStream) Uint8(n int) uint8 {
    return uint8(bs.GetBits(n))
}

func (bs *BitStream) Uint16(n int) uint16 {
    return uint16(bs.GetBits(n))
}

func (bs *BitStream) Uint32(n int) uint32 {
    return uint32(bs.GetBits(n))
}

func (bs *BitStream) GetBytes(n int) []byte {
    if bs.bytesOffset+n > len(bs.bits) {
        panic("OUT OF RANGE")
    }
    if bs.bitsOffset != 0 {
        panic("invaild operation")
    }
    data := make([]byte, n)
    copy(data, bs.bits[bs.bytesOffset:bs.bytesOffset+n])
    bs.bytesOffset += n
    return data
}

//n <= 64
func (bs *BitStream) GetBits(n int) uint64 {
    if bs.bytesOffset >= len(bs.bits) {
        panic("OUT OF RANGE")
    }
    var ret uint64 = 0
    if 8-bs.bitsOffset >= n {
        ret = uint64((bs.bits[bs.bytesOffset] >> (8 - bs.bitsOffset - n)) & BitMask[n-1])
        bs.bitsOffset += n
        if bs.bitsOffset == 8 {
            bs.bytesOffset++
            bs.bitsOffset = 0
        }
    } else {
        ret = uint64(bs.bits[bs.bytesOffset] & BitMask[8-bs.bitsOffset-1])
        bs.bytesOffset++
        n -= 8 - bs.bitsOffset
        bs.bitsOffset = 0
        for n > 0 {
            if bs.bytesOffset >= len(bs.bits) {
                panic("OUT OF RANGE")
            }
            if n >= 8 {
                ret = ret<<8 | uint64(bs.bits[bs.bytesOffset])
                bs.bytesOffset++
                n -= 8
            } else {
                ret = (ret << n) | uint64((bs.bits[bs.bytesOffset]>>(8-n))&BitMask[n-1])
                bs.bitsOffset = n
                break
            }
        }
    }
    return ret
}

func (bs *BitStream) GetBit() uint8 {
    if bs.bytesOffset >= len(bs.bits) {
        panic("OUT OF RANGE")
    }
    ret := bs.bits[bs.bytesOffset] >> (7 - bs.bitsOffset) & 0x01
    bs.bitsOffset++
    if bs.bitsOffset >= 8 {
        bs.bytesOffset++
        bs.bitsOffset = 0
    }
    return ret
}

func (bs *BitStream) SkipBits(n int) {
    bytecount := n / 8
    bitscount := n % 8
    bs.bytesOffset += bytecount
    if bs.bitsOffset+bitscount < 8 {
        bs.bitsOffset += bitscount
    } else {
        bs.bytesOffset += 1
        bs.bitsOffset += bitscount - 8
    }
}

func (bs *BitStream) Markdot() {
    bs.bitsmark = bs.bitsOffset
    bs.bytemark = bs.bytesOffset
}

func (bs *BitStream) DistanceFromMarkDot() int {
    bytecount := bs.bytesOffset - bs.bytemark - 1
    bitscount := bs.bitsOffset + (8 - bs.bitsmark)
    return bytecount*8 + bitscount
}

func (bs *BitStream) RemainBytes() int {
    if bs.bitsOffset > 0 {
        return len(bs.bits) - bs.bytesOffset - 1
    } else {
        return len(bs.bits) - bs.bytesOffset
    }
}

func (bs *BitStream) RemainBits() int {
    if bs.bitsOffset > 0 {
        return bs.RemainBytes()*8 + 8 - bs.bitsOffset
    } else {
        return bs.RemainBytes() * 8
    }

}

func (bs *BitStream) Bits() []byte {
    return bs.bits
}

func (bs *BitStream) RemainData() []byte {
    return bs.bits[bs.bytesOffset:]
}

//无符号哥伦布熵编码
func (bs *BitStream) ReadUE() uint64 {
    leadingZeroBits := 0
    for bs.GetBit() == 0 {
        leadingZeroBits++
    }
    if leadingZeroBits == 0 {
        return 0
    }
    info := bs.GetBits(leadingZeroBits)
    return uint64(1)<<leadingZeroBits - 1 + info
}

//有符号哥伦布熵编码
func (bs *BitStream) ReadSE() int64 {
    v := bs.ReadUE()
    if v%2 == 0 {
        return -1 * int64(v/2)
    } else {
        return int64(v+1) / 2
    }
}

func (bs *BitStream) ByteOffset() int {
    return bs.bytesOffset
}

func (bs *BitStream) UnRead(n int) {
    if n-bs.bitsOffset <= 0 {
        bs.bitsOffset -= n
    } else {
        least := n - bs.bitsOffset
        for least >= 8 {
            bs.bytesOffset--
            least -= 8
        }
        if least > 0 {
            bs.bytesOffset--
            bs.bitsOffset = 8 - least
        }
    }
}

func (bs *BitStream) NextBits(n int) uint64 {
    r := bs.GetBits(n)
    bs.UnRead(n)
    return r
}

func (bs *BitStream) EOS() bool {
    return bs.bytesOffset == len(bs.bits) && bs.bitsOffset == 0
}

type BitStreamWriter struct {
    bits       []byte
    byteoffset int
    bitsoffset int
    bitsmark   int
    bytemark   int
}

func NewBitStreamWriter(n int) *BitStreamWriter {
    return &BitStreamWriter{
        bits:       make([]byte, n),
        byteoffset: 0,
        bitsoffset: 0,
        bitsmark:   0,
        bytemark:   0,
    }
}

func (bsw *BitStreamWriter) expandSpace(n int) {
    if (len(bsw.bits)-bsw.byteoffset-1)*8+8-bsw.bitsoffset < n {
        newlen := 0
        if len(bsw.bits)*8 < n {
            newlen = len(bsw.bits) + n/8 + 1
        } else {
            newlen = len(bsw.bits) * 2
        }
        tmp := make([]byte, newlen)
        copy(tmp, bsw.bits)
        bsw.bits = tmp
    }
}

func (bsw *BitStreamWriter) ByteOffset() int {
    return bsw.byteoffset
}

func (bsw *BitStreamWriter) BitOffset() int {
    return bsw.bitsoffset
}

func (bsw *BitStreamWriter) Markdot() {
    bsw.bitsmark = bsw.bitsoffset
    bsw.bytemark = bsw.byteoffset
}

func (bsw *BitStreamWriter) DistanceFromMarkDot() int {
    bytecount := bsw.byteoffset - bsw.bytemark - 1
    bitscount := bsw.bitsoffset + (8 - bsw.bitsmark)
    return bytecount*8 + bitscount
}

func (bsw *BitStreamWriter) PutByte(v byte) {
    bsw.expandSpace(8)
    if bsw.bitsoffset == 0 {
        bsw.bits[bsw.byteoffset] = v
        bsw.byteoffset++
    } else {
        bsw.bits[bsw.byteoffset] |= v >> byte(bsw.bitsoffset)
        bsw.byteoffset++
        bsw.bits[bsw.byteoffset] = v & BitMask[bsw.bitsoffset-1]
    }
}

func (bsw *BitStreamWriter) PutBytes(v []byte) {
    if bsw.bitsoffset != 0 {
        panic("bsw.bitsoffset > 0")
    }
    bsw.expandSpace(8 * len(v))
    copy(bsw.bits[bsw.byteoffset:], v)
    bsw.byteoffset += len(v)
}

func (bsw *BitStreamWriter) PutRepetValue(v byte, n int) {
    if bsw.bitsoffset != 0 {
        panic("bsw.bitsoffset > 0")
    }
    bsw.expandSpace(8 * n)
    for i := 0; i < n; i++ {
        bsw.bits[bsw.byteoffset] = v
        bsw.byteoffset++
    }
}

func (bsw *BitStreamWriter) PutUint8(v uint8, n int) {
    bsw.PutUint64(uint64(v), n)
}

func (bsw *BitStreamWriter) PutUint16(v uint16, n int) {
    bsw.PutUint64(uint64(v), n)
}

func (bsw *BitStreamWriter) PutUint32(v uint32, n int) {
    bsw.PutUint64(uint64(v), n)
}

func (bsw *BitStreamWriter) PutUint64(v uint64, n int) {
    bsw.expandSpace(n)
    if 8-bsw.bitsoffset >= n {
        bsw.bits[bsw.byteoffset] |= uint8(v) & BitMask[n-1] << (8 - bsw.bitsoffset - n)
        bsw.bitsoffset += n
        if bsw.bitsoffset == 8 {
            bsw.bitsoffset = 0
            bsw.byteoffset++
        }
    } else {
        bsw.bits[bsw.byteoffset] |= uint8(v>>(n-int(8-bsw.bitsoffset))) & BitMask[8-bsw.bitsoffset-1]
        bsw.byteoffset++
        n -= 8 - bsw.bitsoffset
        for n-8 >= 0 {
            bsw.bits[bsw.byteoffset] = uint8(v>>(n-8)) & 0xFF
            bsw.byteoffset++
            n -= 8
        }
        bsw.bitsoffset = n
        if n > 0 {
            bsw.bits[bsw.byteoffset] |= (uint8(v) & BitMask[n-1]) << (8 - n)
        }
    }
}

func (bsw *BitStreamWriter) SetByte(v byte, where int) {
    bsw.bits[where] = v
}

func (bsw *BitStreamWriter) SetUint16(v uint16, where int) {
    binary.BigEndian.PutUint16(bsw.bits[where:where+2], v)
}

func (bsw *BitStreamWriter) Bits() []byte {
    if bsw.byteoffset == len(bsw.bits) {
        return bsw.bits
    }
    if bsw.bitsoffset > 0 {
        return bsw.bits[0 : bsw.byteoffset+1]
    } else {
        return bsw.bits[0:bsw.byteoffset]
    }
}

//用v 填充剩余字节
func (bsw *BitStreamWriter) FillRemainData(v byte) {
    for i := bsw.byteoffset; i < len(bsw.bits); i++ {
        bsw.bits[i] = v
    }
    bsw.byteoffset = len(bsw.bits)
    bsw.bitsoffset = 0
}

func (bsw *BitStreamWriter) Reset() {
    for i := 0; i < len(bsw.bits); i++ {
        bsw.bits[i] = 0
    }
    bsw.bitsmark = 0
    bsw.bytemark = 0
    bsw.bitsoffset = 0
    bsw.byteoffset = 0
}
