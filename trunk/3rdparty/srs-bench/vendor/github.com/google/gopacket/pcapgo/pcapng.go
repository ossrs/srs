// Copyright 2018 The GoPacket Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree.

package pcapgo

import (
	"errors"
	"math"
	"time"

	"github.com/google/gopacket"
	"github.com/google/gopacket/layers"
)

// ErrNgVersionMismatch gets returned for unknown pcapng section versions. This can only happen if ReaderOptions.SkipUnknownVersion == false
var ErrNgVersionMismatch = errors.New("Unknown pcapng Version in Section Header")

// ErrNgLinkTypeMismatch gets returned if the link type of an interface is not the same as the link type from the first interface. This can only happen if ReaderOptions.ErrorOnMismatchingLinkType == true && ReaderOptions.WantMixedLinkType == false
var ErrNgLinkTypeMismatch = errors.New("Link type of current interface is different from first one")

const (
	ngByteOrderMagic = 0x1A2B3C4D

	// We can handle only version 1.0
	ngVersionMajor = 1
	ngVersionMinor = 0
)

type ngBlockType uint32

const (
	ngBlockTypeInterfaceDescriptor ngBlockType = 1          // Interface description block
	ngBlockTypePacket              ngBlockType = 2          // Packet block (deprecated)
	ngBlockTypeSimplePacket        ngBlockType = 3          // Simple packet block
	ngBlockTypeInterfaceStatistics ngBlockType = 5          // Interface statistics block
	ngBlockTypeEnhancedPacket      ngBlockType = 6          // Enhanced packet block
	ngBlockTypeSectionHeader       ngBlockType = 0x0A0D0D0A // Section header block (same in both endians)
)

type ngOptionCode uint16

const (
	ngOptionCodeEndOfOptions    ngOptionCode = iota // end of options. must be at the end of options in a block
	ngOptionCodeComment                             // comment
	ngOptionCodeHardware                            // description of the hardware
	ngOptionCodeOS                                  // name of the operating system
	ngOptionCodeUserApplication                     // name of the application
)

const (
	ngOptionCodeInterfaceName                ngOptionCode = iota + 2 // interface name
	ngOptionCodeInterfaceDescription                                 // interface description
	ngOptionCodeInterfaceIPV4Address                                 // IPv4 network address and netmask for the interface
	ngOptionCodeInterfaceIPV6Address                                 // IPv6 network address and prefix length for the interface
	ngOptionCodeInterfaceMACAddress                                  // interface hardware MAC address
	ngOptionCodeInterfaceEUIAddress                                  // interface hardware EUI address
	ngOptionCodeInterfaceSpeed                                       // interface speed in bits/s
	ngOptionCodeInterfaceTimestampResolution                         // timestamp resolution
	ngOptionCodeInterfaceTimezone                                    // time zone
	ngOptionCodeInterfaceFilter                                      // capture filter
	ngOptionCodeInterfaceOS                                          // operating system
	ngOptionCodeInterfaceFCSLength                                   // length of the Frame Check Sequence in bits
	ngOptionCodeInterfaceTimestampOffset                             // offset (in seconds) that must be added to packet timestamp
)

const (
	ngOptionCodeInterfaceStatisticsStartTime         ngOptionCode = iota + 2 // Start of capture
	ngOptionCodeInterfaceStatisticsEndTime                                   // End of capture
	ngOptionCodeInterfaceStatisticsInterfaceReceived                         // Packets received by physical interface
	ngOptionCodeInterfaceStatisticsInterfaceDropped                          // Packets dropped by physical interface
	ngOptionCodeInterfaceStatisticsFilterAccept                              // Packets accepted by filter
	ngOptionCodeInterfaceStatisticsOSDrop                                    // Packets dropped by operating system
	ngOptionCodeInterfaceStatisticsDelivered                                 // Packets delivered to user
)

// ngOption is a pcapng option
type ngOption struct {
	code   ngOptionCode
	value  []byte
	raw    interface{}
	length uint16
}

// ngBlock is a pcapng block header
type ngBlock struct {
	typ    ngBlockType
	length uint32 // remaining length of block
}

// NgResolution represents a pcapng timestamp resolution
type NgResolution uint8

// Binary returns true if the timestamp resolution is a negative power of two. Otherwise NgResolution is a negative power of 10.
func (r NgResolution) Binary() bool {
	if r&0x80 == 0x80 {
		return true
	}
	return false
}

// Exponent returns the negative exponent of the resolution.
func (r NgResolution) Exponent() uint8 {
	return uint8(r) & 0x7f
}

// ToTimestampResolution converts an NgResolution to a gopaket.TimestampResolution
func (r NgResolution) ToTimestampResolution() (ret gopacket.TimestampResolution) {
	if r.Binary() {
		ret.Base = 2
	} else {
		ret.Base = 10
	}
	ret.Exponent = -int(r.Exponent())
	return
}

// NgNoValue64 is a placeholder for an empty numeric 64 bit value.
const NgNoValue64 = math.MaxUint64

// NgInterfaceStatistics hold the statistic for an interface at a single point in time. These values are already supposed to be accumulated. Most pcapng files contain this information at the end of the file/section.
type NgInterfaceStatistics struct {
	// LastUpdate is the last time the statistics were updated.
	LastUpdate time.Time
	// StartTime is the time packet capture started on this interface. This value might be zero if this option is missing.
	StartTime time.Time
	// EndTime is the time packet capture ended on this interface This value might be zero if this option is missing.
	EndTime time.Time
	// Comment can be an arbitrary comment. This value might be empty if this option is missing.
	Comment string
	// PacketsReceived are the number of received packets. This value might be NoValue64 if this option is missing.
	PacketsReceived uint64
	// PacketsReceived are the number of received packets. This value might be NoValue64 if this option is missing.
	PacketsDropped uint64
}

var ngEmptyStatistics = NgInterfaceStatistics{
	PacketsReceived: NgNoValue64,
	PacketsDropped:  NgNoValue64,
}

// NgInterface holds all the information of a pcapng interface.
type NgInterface struct {
	// Name is the name of the interface. This value might be empty if this option is missing.
	Name string
	// Comment can be an arbitrary comment. This value might be empty if this option is missing.
	Comment string
	// Description is a description of the interface. This value might be empty if this option is missing.
	Description string
	// Filter is the filter used during packet capture. This value might be empty if this option is missing.
	Filter string
	// OS is the operating system this interface was controlled by. This value might be empty if this option is missing.
	OS string
	// LinkType is the linktype of the interface.
	LinkType layers.LinkType
	// TimestampResolution is the timestamp resolution of the packets in the pcapng file belonging to this interface.
	TimestampResolution NgResolution
	// TimestampResolution is the timestamp offset in seconds of the packets in the pcapng file belonging to this interface.
	TimestampOffset uint64
	// SnapLength is the maximum packet length captured by this interface. 0 for unlimited
	SnapLength uint32
	// Statistics holds the interface statistics
	Statistics NgInterfaceStatistics

	secondMask uint64
	scaleUp    uint64
	scaleDown  uint64
}

// Resolution returns the timestamp resolution of acquired timestamps before scaling to NanosecondTimestampResolution.
func (i NgInterface) Resolution() gopacket.TimestampResolution {
	return i.TimestampResolution.ToTimestampResolution()
}

// NgSectionInfo contains additional information of a pcapng section
type NgSectionInfo struct {
	// Hardware is the hardware this file was generated on. This value might be empty if this option is missing.
	Hardware string
	// OS is the operating system this file was generated on. This value might be empty if this option is missing.
	OS string
	// Application is the user space application this file was generated with. This value might be empty if this option is missing.
	Application string
	// Comment can be an arbitrary comment. This value might be empty if this option is missing.
	Comment string
}
