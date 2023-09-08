// Copyright 2012 Google, Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree.

package pcapgo

import (
	"encoding/binary"
	"fmt"
	"io"
	"time"

	"github.com/google/gopacket"
	"github.com/google/gopacket/layers"
)

// Writer wraps an underlying io.Writer to write packet data in PCAP
// format.  See http://wiki.wireshark.org/Development/LibpcapFileFormat
// for information on the file format.
//
// For those that care, we currently write v2.4 files with nanosecond
// or microsecond timestamp resolution and little-endian encoding.
type Writer struct {
	w        io.Writer
	tsScaler int
	// Moving this into the struct seems to save an allocation for each call to writePacketHeader
	buf [16]byte
}

const magicMicroseconds = 0xA1B2C3D4
const versionMajor = 2
const versionMinor = 4

// NewWriterNanos returns a new writer object, for writing packet data out
// to the given writer.  If this is a new empty writer (as opposed to
// an append), you must call WriteFileHeader before WritePacket.  Packet
// timestamps are written with nanosecond precision.
//
//  // Write a new file:
//  f, _ := os.Create("/tmp/file.pcap")
//  w := pcapgo.NewWriterNanos(f)
//  w.WriteFileHeader(65536, layers.LinkTypeEthernet)  // new file, must do this.
//  w.WritePacket(gopacket.CaptureInfo{...}, data1)
//  f.Close()
//  // Append to existing file (must have same snaplen and linktype)
//  f2, _ := os.OpenFile("/tmp/fileNano.pcap", os.O_APPEND, 0700)
//  w2 := pcapgo.NewWriter(f2)
//  // no need for file header, it's already written.
//  w2.WritePacket(gopacket.CaptureInfo{...}, data2)
//  f2.Close()
func NewWriterNanos(w io.Writer) *Writer {
	return &Writer{w: w, tsScaler: nanosPerNano}
}

// NewWriter returns a new writer object, for writing packet data out
// to the given writer.  If this is a new empty writer (as opposed to
// an append), you must call WriteFileHeader before WritePacket.
// Packet timestamps are written witn microsecond precision.
//
//  // Write a new file:
//  f, _ := os.Create("/tmp/file.pcap")
//  w := pcapgo.NewWriter(f)
//  w.WriteFileHeader(65536, layers.LinkTypeEthernet)  // new file, must do this.
//  w.WritePacket(gopacket.CaptureInfo{...}, data1)
//  f.Close()
//  // Append to existing file (must have same snaplen and linktype)
//  f2, _ := os.OpenFile("/tmp/file.pcap", os.O_APPEND, 0700)
//  w2 := pcapgo.NewWriter(f2)
//  // no need for file header, it's already written.
//  w2.WritePacket(gopacket.CaptureInfo{...}, data2)
//  f2.Close()
func NewWriter(w io.Writer) *Writer {
	return &Writer{w: w, tsScaler: nanosPerMicro}
}

// WriteFileHeader writes a file header out to the writer.
// This must be called exactly once per output.
func (w *Writer) WriteFileHeader(snaplen uint32, linktype layers.LinkType) error {
	var buf [24]byte
	if w.tsScaler == nanosPerMicro {
		binary.LittleEndian.PutUint32(buf[0:4], magicMicroseconds)
	} else {
		binary.LittleEndian.PutUint32(buf[0:4], magicNanoseconds)
	}
	binary.LittleEndian.PutUint16(buf[4:6], versionMajor)
	binary.LittleEndian.PutUint16(buf[6:8], versionMinor)
	// bytes 8:12 stay 0 (timezone = UTC)
	// bytes 12:16 stay 0 (sigfigs is always set to zero, according to
	//   http://wiki.wireshark.org/Development/LibpcapFileFormat
	binary.LittleEndian.PutUint32(buf[16:20], snaplen)
	binary.LittleEndian.PutUint32(buf[20:24], uint32(linktype))
	_, err := w.w.Write(buf[:])
	return err
}

const nanosPerMicro = 1000
const nanosPerNano = 1

func (w *Writer) writePacketHeader(ci gopacket.CaptureInfo) error {
	t := ci.Timestamp
	if t.IsZero() {
		t = time.Now()
	}
	secs := t.Unix()
	usecs := t.Nanosecond() / w.tsScaler
	binary.LittleEndian.PutUint32(w.buf[0:4], uint32(secs))
	binary.LittleEndian.PutUint32(w.buf[4:8], uint32(usecs))
	binary.LittleEndian.PutUint32(w.buf[8:12], uint32(ci.CaptureLength))
	binary.LittleEndian.PutUint32(w.buf[12:16], uint32(ci.Length))
	_, err := w.w.Write(w.buf[:])
	return err
}

// WritePacket writes the given packet data out to the file.
func (w *Writer) WritePacket(ci gopacket.CaptureInfo, data []byte) error {
	if ci.CaptureLength != len(data) {
		return fmt.Errorf("capture length %d does not match data length %d", ci.CaptureLength, len(data))
	}
	if ci.CaptureLength > ci.Length {
		return fmt.Errorf("invalid capture info %+v:  capture length > length", ci)
	}
	if err := w.writePacketHeader(ci); err != nil {
		return fmt.Errorf("error writing packet header: %v", err)
	}
	_, err := w.w.Write(data)
	return err
}
