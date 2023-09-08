// Copyright 2018 The GoPacket Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree.

package pcapgo

import (
	"bufio"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"time"

	"github.com/google/gopacket"
	"github.com/google/gopacket/layers"
)

// NgReaderOptions holds options for reading a pcapng file
type NgReaderOptions struct {
	// WantMixedLinkType enables reading a pcapng file containing multiple interfaces with varying link types. If false all link types must match, which is the libpcap behaviour and LinkType returns the link type of the first interface.
	// If true the link type of the packet is also exposed via ci.AncillaryData[0].
	WantMixedLinkType bool
	// ErrorOnMismatchingLinkType enables returning an error if a packet with a link type not matching the first interface is encountered and WantMixedLinkType == false.
	// If false packets those packets are just silently ignored, which is the libpcap behaviour.
	ErrorOnMismatchingLinkType bool
	// SkipUnknownVersion enables automatically skipping sections with an unknown version, which is recommended by the pcapng standard. Otherwise ErrVersionMismatch is returned.
	SkipUnknownVersion bool
	// SectionEndCallback gets called at the end of a section (execept for the last section, which is ends on EOF). The current list of interfaces and additional section information is provided.
	// This is a good way to read interface statistics.
	SectionEndCallback func([]NgInterface, NgSectionInfo)
	// StatisticsCallback is called when a interface statistics block is read. The interface id and the read statistics are provided.
	StatisticsCallback func(int, NgInterfaceStatistics)
}

// DefaultNgReaderOptions provides sane defaults for a pcapng reader.
var DefaultNgReaderOptions = NgReaderOptions{}

// NgReader wraps an underlying bufio.NgReader to read packet data in pcapng.
type NgReader struct {
	r                 *bufio.Reader
	options           NgReaderOptions
	sectionInfo       NgSectionInfo
	linkType          layers.LinkType
	ifaces            []NgInterface
	currentBlock      ngBlock
	currentOption     ngOption
	buf               [24]byte
	packetBuf         []byte
	ci                gopacket.CaptureInfo
	ancil             [1]interface{}
	blen              int
	firstSectionFound bool
	activeSection     bool
	bigEndian         bool
}

// NewNgReader initializes a new writer, reads the first section header, and if necessary according to the options the first interface.
func NewNgReader(r io.Reader, options NgReaderOptions) (*NgReader, error) {
	ret := &NgReader{
		r: bufio.NewReader(r),
		currentOption: ngOption{
			value: make([]byte, 1024),
		},
		options: options,
	}

	//pcapng _must_ start with a section header
	if err := ret.readBlock(); err != nil {
		return nil, err
	}
	if ret.currentBlock.typ != ngBlockTypeSectionHeader {
		return nil, fmt.Errorf("Unknown magic %x", ret.currentBlock.typ)
	}

	if err := ret.readSectionHeader(); err != nil {
		return nil, err
	}

	return ret, nil
}

// First a couple of helper functions to speed things up

// This is way faster than calling io.ReadFull since io.ReadFull needs an itab lookup, does an additional function call into ReadAtLeast, and ReadAtLeast does additional stuff we don't need
// Additionally this removes the bounds check compared to io.ReadFull due to the use of uint
func (r *NgReader) readBytes(buffer []byte) error {
	n := uint(0)
	for n < uint(len(buffer)) {
		nn, err := r.r.Read(buffer[n:])
		n += uint(nn)
		if err != nil {
			return err
		}
	}
	return nil
}

// The following functions make the binary.* functions inlineable (except for getUint64, which is too big, but not in any hot path anyway)
// Compared to storing binary.*Endian in a binary.ByteOrder this shaves off about 20% for (ZeroCopy)ReadPacketData, which is caused by the needed itab lookup + indirect go call
func (r *NgReader) getUint16(buffer []byte) uint16 {
	if r.bigEndian {
		return binary.BigEndian.Uint16(buffer)
	}
	return binary.LittleEndian.Uint16(buffer)
}

func (r *NgReader) getUint32(buffer []byte) uint32 {
	if r.bigEndian {
		return binary.BigEndian.Uint32(buffer)
	}
	return binary.LittleEndian.Uint32(buffer)
}

func (r *NgReader) getUint64(buffer []byte) uint64 {
	if r.bigEndian {
		return binary.BigEndian.Uint64(buffer)
	}
	return binary.LittleEndian.Uint64(buffer)
}

// Now the pcapng implementation

// readBlock reads a the blocktype and length from the file. If the type is a section header, endianess is also read.
func (r *NgReader) readBlock() error {
	if err := r.readBytes(r.buf[0:8]); err != nil {
		return err
	}
	r.currentBlock.typ = ngBlockType(r.getUint32(r.buf[0:4]))
	// The next part is a bit fucked up since a section header could change the endianess...
	// So first read then length just into a buffer, check if its a section header and then do the endianess part...
	if r.currentBlock.typ == ngBlockTypeSectionHeader {
		if err := r.readBytes(r.buf[8:12]); err != nil {
			return err
		}
		if binary.BigEndian.Uint32(r.buf[8:12]) == ngByteOrderMagic {
			r.bigEndian = true
		} else if binary.LittleEndian.Uint32(r.buf[8:12]) == ngByteOrderMagic {
			r.bigEndian = false
		} else {
			return errors.New("Wrong byte order value in Section Header")
		}
		// Set length to remaining length (length - (type + lengthfield = 8) - 4 for byteOrderMagic)
		r.currentBlock.length = r.getUint32(r.buf[4:8]) - 8 - 4
		return nil
	}
	// Set length to remaining length (length - (type + lengthfield = 8)
	r.currentBlock.length = r.getUint32(r.buf[4:8]) - 8
	return nil
}

// readOption reads a single arbitrary option (type and value). If there is no space left for options and end of options is missing, it is faked.
func (r *NgReader) readOption() error {
	if r.currentBlock.length == 4 {
		// no more options
		r.currentOption.code = ngOptionCodeEndOfOptions
		return nil
	}
	if err := r.readBytes(r.buf[:4]); err != nil {
		return err
	}
	r.currentBlock.length -= 4
	r.currentOption.code = ngOptionCode(r.getUint16(r.buf[:2]))
	length := r.getUint16(r.buf[2:4])
	if r.currentOption.code == ngOptionCodeEndOfOptions {
		if length != 0 {
			return errors.New("End of Options must be zero length")
		}
		return nil
	}
	if length != 0 {
		if length < uint16(cap(r.currentOption.value)) {
			r.currentOption.value = r.currentOption.value[:length]
		} else {
			r.currentOption.value = make([]byte, length)
		}
		if err := r.readBytes(r.currentOption.value); err != nil {
			return err
		}
		//consume padding
		padding := length % 4
		if padding > 0 {
			padding = 4 - padding
			if _, err := r.r.Discard(int(padding)); err != nil {
				return err
			}
		}
		r.currentBlock.length -= uint32(length + padding)
	}
	return nil
}

// readSectionHeader parses the full section header and implements section skipping in case of version mismatch
// if needed, the first interface is read
func (r *NgReader) readSectionHeader() error {
	if r.options.SectionEndCallback != nil && r.activeSection {
		interfaces := make([]NgInterface, len(r.ifaces))
		for i := range r.ifaces {
			interfaces[i] = r.ifaces[i]
		}
		r.options.SectionEndCallback(interfaces, r.sectionInfo)
	}
	// clear the interfaces
	r.ifaces = r.ifaces[:0]
	r.activeSection = false

RESTART:
	// read major, minor, section length
	if err := r.readBytes(r.buf[:12]); err != nil {
		return err
	}
	r.currentBlock.length -= 12

	vMajor := r.getUint16(r.buf[0:2])
	vMinor := r.getUint16(r.buf[2:4])
	if vMajor != ngVersionMajor || vMinor != ngVersionMinor {
		if !r.options.SkipUnknownVersion {
			// Well the standard actually says to skip unknown version section headers,
			// but this would mean user would be kept in the dark about whats going on...
			return ErrNgVersionMismatch
		}
		if _, err := r.r.Discard(int(r.currentBlock.length)); err != nil {
			return err
		}
		if err := r.skipSection(); err != nil {
			return err
		}
		goto RESTART
	}

	var section NgSectionInfo

OPTIONS:
	for {
		if err := r.readOption(); err != nil {
			return err
		}
		switch r.currentOption.code {
		case ngOptionCodeEndOfOptions:
			break OPTIONS
		case ngOptionCodeComment:
			section.Comment = string(r.currentOption.value)
		case ngOptionCodeHardware:
			section.Hardware = string(r.currentOption.value)
		case ngOptionCodeOS:
			section.OS = string(r.currentOption.value)
		case ngOptionCodeUserApplication:
			section.Application = string(r.currentOption.value)
		}
	}

	if _, err := r.r.Discard(int(r.currentBlock.length)); err != nil {
		return err
	}
	r.activeSection = true
	r.sectionInfo = section

	if !r.options.WantMixedLinkType {
		// If we don't want mixed link type, we need the first interface to fill Reader.LinkType()
		// This handles most of the pcapngs out there, since they start with an IDB
		if err := r.firstInterface(); err != nil {
			return err
		}
	}

	return nil
}

// skipSection skips blocks until the next section
func (r *NgReader) skipSection() error {
	for {
		if err := r.readBlock(); err != nil {
			return err
		}
		if r.currentBlock.typ == ngBlockTypeSectionHeader {
			return nil
		}
		if _, err := r.r.Discard(int(r.currentBlock.length)); err != nil {
			return err
		}
	}
}

// SkipSection skips the contents of the rest of the current section and reads the next section header.
func (r *NgReader) SkipSection() error {
	if err := r.skipSection(); err != nil {
		return err
	}
	return r.readSectionHeader()
}

// firstInterface reads the first interface from the section and panics if a packet is encountered.
func (r *NgReader) firstInterface() error {
	for {
		if err := r.readBlock(); err != nil {
			return err
		}
		switch r.currentBlock.typ {
		case ngBlockTypeInterfaceDescriptor:
			if err := r.readInterfaceDescriptor(); err != nil {
				return err
			}
			if !r.firstSectionFound {
				r.linkType = r.ifaces[0].LinkType
				r.firstSectionFound = true
			} else if r.linkType != r.ifaces[0].LinkType {
				if r.options.ErrorOnMismatchingLinkType {
					return ErrNgLinkTypeMismatch
				}
				continue
			}
			return nil
		case ngBlockTypePacket, ngBlockTypeEnhancedPacket, ngBlockTypeSimplePacket, ngBlockTypeInterfaceStatistics:
			return errors.New("A section must have an interface before a packet block")
		}
		if _, err := r.r.Discard(int(r.currentBlock.length)); err != nil {
			return err
		}
	}
}

// readInterfaceDescriptor parses an interface descriptor, prepares timing calculation, and adds the interface details to the current list
func (r *NgReader) readInterfaceDescriptor() error {
	if err := r.readBytes(r.buf[:8]); err != nil {
		return err
	}
	r.currentBlock.length -= 8
	var intf NgInterface
	intf.LinkType = layers.LinkType(r.getUint16(r.buf[:2]))
	intf.SnapLength = r.getUint32(r.buf[4:8])

OPTIONS:
	for {
		if err := r.readOption(); err != nil {
			return err
		}
		switch r.currentOption.code {
		case ngOptionCodeEndOfOptions:
			break OPTIONS
		case ngOptionCodeInterfaceName:
			intf.Name = string(r.currentOption.value)
		case ngOptionCodeComment:
			intf.Comment = string(r.currentOption.value)
		case ngOptionCodeInterfaceDescription:
			intf.Description = string(r.currentOption.value)
		case ngOptionCodeInterfaceFilter:
			// ignore filter type (first byte) since it is not specified
			intf.Filter = string(r.currentOption.value[1:])
		case ngOptionCodeInterfaceOS:
			intf.OS = string(r.currentOption.value)
		case ngOptionCodeInterfaceTimestampOffset:
			intf.TimestampOffset = r.getUint64(r.currentOption.value[:8])
		case ngOptionCodeInterfaceTimestampResolution:
			intf.TimestampResolution = NgResolution(r.currentOption.value[0])
		}
	}
	if _, err := r.r.Discard(int(r.currentBlock.length)); err != nil {
		return err
	}
	if intf.TimestampResolution == 0 {
		intf.TimestampResolution = 6
	}

	//parse options
	if intf.TimestampResolution.Binary() {
		//negative power of 2
		intf.secondMask = 1 << intf.TimestampResolution.Exponent()
	} else {
		//negative power of 10
		intf.secondMask = 1
		for j := uint8(0); j < intf.TimestampResolution.Exponent(); j++ {
			intf.secondMask *= 10
		}
	}
	intf.scaleDown = 1
	intf.scaleUp = 1
	if intf.secondMask < 1e9 {
		intf.scaleUp = 1e9 / intf.secondMask
	} else {
		intf.scaleDown = intf.secondMask / 1e9
	}
	r.ifaces = append(r.ifaces, intf)
	return nil
}

// convertTime adds offset + shifts the given time value according to the given interface
func (r *NgReader) convertTime(ifaceID int, ts uint64) (int64, int64) {
	iface := r.ifaces[ifaceID]
	return int64(ts/iface.secondMask + iface.TimestampOffset), int64(ts % iface.secondMask * iface.scaleUp / iface.scaleDown)
}

// readInterfaceStatistics updates the statistics of the given interface
func (r *NgReader) readInterfaceStatistics() error {
	if err := r.readBytes(r.buf[:12]); err != nil {
		return err
	}
	r.currentBlock.length -= 12
	ifaceID := int(r.getUint32(r.buf[:4]))
	ts := uint64(r.getUint32(r.buf[4:8]))<<32 | uint64(r.getUint32(r.buf[8:12]))
	if int(ifaceID) >= len(r.ifaces) {
		return fmt.Errorf("Interface id %d not present in section (have only %d interfaces)", ifaceID, len(r.ifaces))
	}
	stats := &r.ifaces[ifaceID].Statistics
	*stats = ngEmptyStatistics
	stats.LastUpdate = time.Unix(r.convertTime(ifaceID, ts)).UTC()

OPTIONS:
	for {
		if err := r.readOption(); err != nil {
			return err
		}
		switch r.currentOption.code {
		case ngOptionCodeEndOfOptions:
			break OPTIONS
		case ngOptionCodeComment:
			stats.Comment = string(r.currentOption.value)
		case ngOptionCodeInterfaceStatisticsStartTime:
			ts = uint64(r.getUint32(r.currentOption.value[:4]))<<32 | uint64(r.getUint32(r.currentOption.value[4:8]))
			stats.StartTime = time.Unix(r.convertTime(ifaceID, ts)).UTC()
		case ngOptionCodeInterfaceStatisticsEndTime:
			ts = uint64(r.getUint32(r.currentOption.value[:4]))<<32 | uint64(r.getUint32(r.currentOption.value[4:8]))
			stats.EndTime = time.Unix(r.convertTime(ifaceID, ts)).UTC()
		case ngOptionCodeInterfaceStatisticsInterfaceReceived:
			stats.PacketsReceived = r.getUint64(r.currentOption.value[:8])
		case ngOptionCodeInterfaceStatisticsInterfaceDropped:
			stats.PacketsDropped = r.getUint64(r.currentOption.value[:8])
		}
	}
	if _, err := r.r.Discard(int(r.currentBlock.length)); err != nil {
		return err
	}
	if r.options.StatisticsCallback != nil {
		r.options.StatisticsCallback(ifaceID, *stats)
	}
	return nil
}

// readPacketHeader looks for a packet (enhanced, simple, or packet) and parses the header.
// If an interface descriptor, an interface statistics block, or a section header is encountered, those are handled accordingly.
// All other block types are skipped. New block types must be added here.
func (r *NgReader) readPacketHeader() error {
RESTART:
FIND_PACKET:
	for {
		if err := r.readBlock(); err != nil {
			return err
		}
		switch r.currentBlock.typ {
		case ngBlockTypeEnhancedPacket:
			if err := r.readBytes(r.buf[:20]); err != nil {
				return err
			}
			r.currentBlock.length -= 20
			r.ci.InterfaceIndex = int(r.getUint32(r.buf[:4]))
			if r.ci.InterfaceIndex >= len(r.ifaces) {
				return fmt.Errorf("Interface id %d not present in section (have only %d interfaces)", r.ci.InterfaceIndex, len(r.ifaces))
			}
			r.ci.Timestamp = time.Unix(r.convertTime(r.ci.InterfaceIndex, uint64(r.getUint32(r.buf[4:8]))<<32|uint64(r.getUint32(r.buf[8:12])))).UTC()
			r.ci.CaptureLength = int(r.getUint32(r.buf[12:16]))
			r.ci.Length = int(r.getUint32(r.buf[16:20]))
			break FIND_PACKET
		case ngBlockTypeSimplePacket:
			if err := r.readBytes(r.buf[:4]); err != nil {
				return err
			}
			r.currentBlock.length -= 4
			r.ci.Timestamp = time.Time{}
			r.ci.InterfaceIndex = 0
			r.ci.Length = int(r.getUint32(r.buf[:4]))
			r.ci.CaptureLength = r.ci.Length
			if len(r.ifaces) == 0 {
				return errors.New("At least one Interface is needed for a packet")
			}
			if r.ifaces[0].SnapLength != 0 && uint32(r.ci.CaptureLength) > r.ifaces[0].SnapLength {
				r.ci.CaptureLength = int(r.ifaces[0].SnapLength)
			}
			break FIND_PACKET
		case ngBlockTypeInterfaceDescriptor:
			if err := r.readInterfaceDescriptor(); err != nil {
				return err
			}
		case ngBlockTypeInterfaceStatistics:
			if err := r.readInterfaceStatistics(); err != nil {
				return err
			}
		case ngBlockTypeSectionHeader:
			if err := r.readSectionHeader(); err != nil {
				return err
			}
		case ngBlockTypePacket:
			if err := r.readBytes(r.buf[:20]); err != nil {
				return err
			}
			r.currentBlock.length -= 20
			r.ci.InterfaceIndex = int(r.getUint16(r.buf[0:2]))
			if r.ci.InterfaceIndex >= len(r.ifaces) {
				return fmt.Errorf("Interface id %d not present in section (have only %d interfaces)", r.ci.InterfaceIndex, len(r.ifaces))
			}
			r.ci.Timestamp = time.Unix(r.convertTime(r.ci.InterfaceIndex, uint64(r.getUint32(r.buf[4:8]))<<32|uint64(r.getUint32(r.buf[8:12])))).UTC()
			r.ci.CaptureLength = int(r.getUint32(r.buf[12:16]))
			r.ci.Length = int(r.getUint32(r.buf[16:20]))
			break FIND_PACKET
		default:
			if _, err := r.r.Discard(int(r.currentBlock.length)); err != nil {
				return err
			}
		}
	}
	if !r.options.WantMixedLinkType {
		if r.ifaces[r.ci.InterfaceIndex].LinkType != r.linkType {
			if _, err := r.r.Discard(int(r.currentBlock.length)); err != nil {
				return err
			}
			if r.options.ErrorOnMismatchingLinkType {
				return ErrNgLinkTypeMismatch
			}
			goto RESTART
		}
		return nil
	}
	r.ancil[0] = r.ifaces[r.ci.InterfaceIndex].LinkType
	return nil
}

// ReadPacketData returns the next packet available from this data source.
// If WantMixedLinkType is true, ci.AncillaryData[0] contains the link type.
func (r *NgReader) ReadPacketData() (data []byte, ci gopacket.CaptureInfo, err error) {
	if err = r.readPacketHeader(); err != nil {
		return
	}
	ci = r.ci
	if r.options.WantMixedLinkType {
		ci.AncillaryData = make([]interface{}, 1)
		ci.AncillaryData[0] = r.ancil[0]
	}
	data = make([]byte, r.ci.CaptureLength)
	if err = r.readBytes(data); err != nil {
		return
	}
	// handle options somehow - this would be expensive
	_, err = r.r.Discard(int(r.currentBlock.length) - r.ci.CaptureLength)
	return
}

// ZeroCopyReadPacketData returns the next packet available from this data source.
// If WantMixedLinkType is true, ci.AncillaryData[0] contains the link type.
// Warning: Like data, ci.AncillaryData is also reused and overwritten on the next call to ZeroCopyReadPacketData.
//
// It is not true zero copy, as data is still copied from the underlying reader. However,
// this method avoids allocating heap memory for every packet.
func (r *NgReader) ZeroCopyReadPacketData() (data []byte, ci gopacket.CaptureInfo, err error) {
	if err = r.readPacketHeader(); err != nil {
		return
	}
	ci = r.ci
	if r.options.WantMixedLinkType {
		ci.AncillaryData = r.ancil[:]
	}
	if cap(r.packetBuf) < ci.CaptureLength {
		snaplen := int(r.ifaces[ci.InterfaceIndex].SnapLength)
		if snaplen < ci.CaptureLength {
			snaplen = ci.CaptureLength
		}
		r.packetBuf = make([]byte, snaplen)
	}
	data = r.packetBuf[:ci.CaptureLength]
	if err = r.readBytes(data); err != nil {
		return
	}
	// handle options somehow - this would be expensive
	_, err = r.r.Discard(int(r.currentBlock.length) - ci.CaptureLength)
	return
}

// LinkType returns the link type of the first interface, as a layers.LinkType. This is only valid, if WantMixedLinkType is false.
func (r *NgReader) LinkType() layers.LinkType {
	return r.linkType
}

// SectionInfo returns information about the current section.
func (r *NgReader) SectionInfo() NgSectionInfo {
	return r.sectionInfo
}

// Interface returns interface information and statistics of interface with the given id.
func (r *NgReader) Interface(i int) (NgInterface, error) {
	if i >= len(r.ifaces) || i < 0 {
		return NgInterface{}, fmt.Errorf("Interface %d invalid. There are only %d interfaces", i, len(r.ifaces))
	}
	return r.ifaces[i], nil
}

// NInterfaces returns the current number of interfaces.
func (r *NgReader) NInterfaces() int {
	return len(r.ifaces)
}

// Resolution returns the timestamp resolution of acquired timestamps before scaling to NanosecondTimestampResolution.
func (r *NgReader) Resolution() gopacket.TimestampResolution {
	if r.options.WantMixedLinkType {
		return gopacket.TimestampResolution{}
	}
	return r.ifaces[0].Resolution()
}
