// Copyright 2014 Damjan Cvetko. All rights reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree.

package pcapgo

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"time"

	"bufio"
	"compress/gzip"

	"github.com/google/gopacket"
	"github.com/google/gopacket/layers"
)

// Reader wraps an underlying io.Reader to read packet data in PCAP
// format.  See http://wiki.wireshark.org/Development/LibpcapFileFormat
// for information on the file format.
//
// We currenty read v2.4 file format with nanosecond and microsecdond
// timestamp resolution in little-endian and big-endian encoding.
//
// If the PCAP data is gzip compressed it is transparently uncompressed
// by wrapping the given io.Reader with a gzip.Reader.
type Reader struct {
	r              io.Reader
	byteOrder      binary.ByteOrder
	nanoSecsFactor uint32
	versionMajor   uint16
	versionMinor   uint16
	// timezone
	// sigfigs
	snaplen  uint32
	linkType layers.LinkType
	// reusable buffer
	buf [16]byte
	// buffer for ZeroCopyReadPacketData
	packetBuf []byte
}

const magicNanoseconds = 0xA1B23C4D
const magicMicrosecondsBigendian = 0xD4C3B2A1
const magicNanosecondsBigendian = 0x4D3CB2A1

const magicGzip1 = 0x1f
const magicGzip2 = 0x8b

// NewReader returns a new reader object, for reading packet data from
// the given reader. The reader must be open and header data is
// read from it at this point.
// If the file format is not supported an error is returned
//
//  // Create new reader:
//  f, _ := os.Open("/tmp/file.pcap")
//  defer f.Close()
//  r, err := NewReader(f)
//  data, ci, err := r.ReadPacketData()
func NewReader(r io.Reader) (*Reader, error) {
	ret := Reader{r: r}
	if err := ret.readHeader(); err != nil {
		return nil, err
	}
	return &ret, nil
}

func (r *Reader) readHeader() error {
	br := bufio.NewReader(r.r)
	gzipMagic, err := br.Peek(2)
	if err != nil {
		return err
	}

	if gzipMagic[0] == magicGzip1 && gzipMagic[1] == magicGzip2 {
		if r.r, err = gzip.NewReader(br); err != nil {
			return err
		}
	} else {
		r.r = br
	}

	buf := make([]byte, 24)
	if n, err := io.ReadFull(r.r, buf); err != nil {
		return err
	} else if n < 24 {
		return errors.New("Not enough data for read")
	}
	if magic := binary.LittleEndian.Uint32(buf[0:4]); magic == magicNanoseconds {
		r.byteOrder = binary.LittleEndian
		r.nanoSecsFactor = 1
	} else if magic == magicNanosecondsBigendian {
		r.byteOrder = binary.BigEndian
		r.nanoSecsFactor = 1
	} else if magic == magicMicroseconds {
		r.byteOrder = binary.LittleEndian
		r.nanoSecsFactor = 1000
	} else if magic == magicMicrosecondsBigendian {
		r.byteOrder = binary.BigEndian
		r.nanoSecsFactor = 1000
	} else {
		return fmt.Errorf("Unknown magic %x", magic)
	}
	if r.versionMajor = r.byteOrder.Uint16(buf[4:6]); r.versionMajor != versionMajor {
		return fmt.Errorf("Unknown major version %d", r.versionMajor)
	}
	if r.versionMinor = r.byteOrder.Uint16(buf[6:8]); r.versionMinor != versionMinor {
		return fmt.Errorf("Unknown minor version %d", r.versionMinor)
	}
	// ignore timezone 8:12 and sigfigs 12:16
	r.snaplen = r.byteOrder.Uint32(buf[16:20])
	r.linkType = layers.LinkType(r.byteOrder.Uint32(buf[20:24]))
	return nil
}

// ReadPacketData reads next packet from file.
func (r *Reader) ReadPacketData() (data []byte, ci gopacket.CaptureInfo, err error) {
	if ci, err = r.readPacketHeader(); err != nil {
		return
	}
	if ci.CaptureLength > int(r.snaplen) {
		err = fmt.Errorf("capture length exceeds snap length: %d > %d", ci.CaptureLength, r.snaplen)
		return
	}
	if ci.CaptureLength > ci.Length {
		err = fmt.Errorf("capture length exceeds original packet length: %d > %d", ci.CaptureLength, ci.Length)
		return
	}
	data = make([]byte, ci.CaptureLength)
	_, err = io.ReadFull(r.r, data)
	return data, ci, err
}

// ZeroCopyReadPacketData reads next packet from file. The data buffer is owned by the Reader,
// and each call to ZeroCopyReadPacketData invalidates data returned by the previous one.
//
// It is not true zero copy, as data is still copied from the underlying reader. However,
// this method avoids allocating heap memory for every packet.
func (r *Reader) ZeroCopyReadPacketData() (data []byte, ci gopacket.CaptureInfo, err error) {
	if ci, err = r.readPacketHeader(); err != nil {
		return
	}
	if ci.CaptureLength > int(r.snaplen) {
		err = fmt.Errorf("capture length exceeds snap length: %d > %d", ci.CaptureLength, r.snaplen)
		return
	}
	if ci.CaptureLength > ci.Length {
		err = fmt.Errorf("capture length exceeds original packet length: %d > %d", ci.CaptureLength, ci.Length)
		return
	}

	if cap(r.packetBuf) < ci.CaptureLength {
		snaplen := int(r.snaplen)
		if snaplen < ci.CaptureLength {
			snaplen = ci.CaptureLength
		}
		r.packetBuf = make([]byte, snaplen)
	}
	data = r.packetBuf[:ci.CaptureLength]
	_, err = io.ReadFull(r.r, data)
	return data, ci, err
}

func (r *Reader) readPacketHeader() (ci gopacket.CaptureInfo, err error) {
	if _, err = io.ReadFull(r.r, r.buf[:]); err != nil {
		return
	}
	ci.Timestamp = time.Unix(int64(r.byteOrder.Uint32(r.buf[0:4])), int64(r.byteOrder.Uint32(r.buf[4:8])*r.nanoSecsFactor)).UTC()
	ci.CaptureLength = int(r.byteOrder.Uint32(r.buf[8:12]))
	ci.Length = int(r.byteOrder.Uint32(r.buf[12:16]))
	return
}

// LinkType returns network, as a layers.LinkType.
func (r *Reader) LinkType() layers.LinkType {
	return r.linkType
}

// Snaplen returns the snapshot length of the capture file.
func (r *Reader) Snaplen() uint32 {
	return r.snaplen
}

// SetSnaplen sets the snapshot length of the capture file.
//
// This is useful when a pcap file contains packets bigger than then snaplen.
// Pcapgo will error when reading packets bigger than snaplen, then it dumps those
// packets and reads the next 16 bytes, which are part of the "faulty" packet's payload, but pcapgo
// thinks it's the next header, which is probably also faulty because it's not really a packet header.
// This can lead to a lot of faulty reads.
//
// The SetSnaplen function can be used to set a bigger snaplen to prevent those read errors.
//
// This snaplen situation can happen when a pcap writer doesn't truncate packets to the snaplen size while writing packets to file.
// E.g. In Python, dpkt.pcap.Writer sets snaplen by default to 1500 (https://dpkt.readthedocs.io/en/latest/api/api_auto.html#dpkt.pcap.Writer)
// but doesn't enforce this when writing packets (https://dpkt.readthedocs.io/en/latest/_modules/dpkt/pcap.html#Writer.writepkt).
// When reading, tools like tcpdump, tcpslice, mergecap and wireshark ignore the snaplen and use
// their own defined snaplen.
// E.g. When reading packets, tcpdump defines MAXIMUM_SNAPLEN (https://github.com/the-tcpdump-group/tcpdump/blob/6e80fcdbe9c41366df3fa244ffe4ac8cce2ab597/netdissect.h#L290)
// and uses it (https://github.com/the-tcpdump-group/tcpdump/blob/66384fa15b04b47ad08c063d4728df3b9c1c0677/print.c#L343-L358).
//
// For further reading:
//  - https://github.com/the-tcpdump-group/tcpdump/issues/389
//  - https://bugs.wireshark.org/bugzilla/show_bug.cgi?id=8808
//  - https://www.wireshark.org/lists/wireshark-dev/201307/msg00061.html
//  - https://github.com/wireshark/wireshark/blob/bfd51199e707c1d5c28732be34b44a9ee8a91cd8/wiretap/pcap-common.c#L723-L742
//    - https://github.com/wireshark/wireshark/blob/f07fb6cdfc0904905627707b88450054e921f092/wiretap/libpcap.c#L592-L598
//    - https://github.com/wireshark/wireshark/blob/f07fb6cdfc0904905627707b88450054e921f092/wiretap/libpcap.c#L714-L727
//  - https://github.com/the-tcpdump-group/tcpdump/commit/d033c1bc381c76d13e4aface97a4f4ec8c3beca2
//  - https://github.com/the-tcpdump-group/tcpdump/blob/88e87cb2cb74c5f939792171379acd9e0efd8b9a/netdissect.h#L263-L290
func (r *Reader) SetSnaplen(newSnaplen uint32) {
	r.snaplen = newSnaplen
}

// Reader formater
func (r *Reader) String() string {
	return fmt.Sprintf("PcapFile  maj: %x min: %x snaplen: %d linktype: %s", r.versionMajor, r.versionMinor, r.snaplen, r.linkType)
}

// Resolution returns the timestamp resolution of acquired timestamps before scaling to NanosecondTimestampResolution.
func (r *Reader) Resolution() gopacket.TimestampResolution {
	if r.nanoSecsFactor == 1 {
		return gopacket.TimestampResolutionMicrosecond
	}
	return gopacket.TimestampResolutionNanosecond
}
