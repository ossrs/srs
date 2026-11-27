// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"bytes"
	"errors"
	"fmt"
	"math/rand"
	"net"
	"os"
	"time"

	"github.com/datarhei/gosrt/circular"
	"github.com/datarhei/gosrt/packet"
)

// SRT versions and handshake values of a raw caller, as libsrt 1.5 sends
// them.
const (
	rawSRTVersion = 0x010506
	rawMTU        = 1500
	rawFlowWindow = 8192
	// rawInductionExt is the extension field of a caller's induction, the
	// UDT socket type DGRAM.
	rawInductionExt = 2
)

// MaxStreamID is the longest stream ID SRT carries, in bytes.
const MaxStreamID = 512

// Raw is a UDP socket that speaks SRT by hand, one packet at a time, so a
// case sends a listener what no library sends: garbage, truncated or
// oversized handshakes, a stream ID over MaxStreamID, a control packet with a
// short or long body, or data before the handshake. After Connect it is a
// caller with a connection of its own: SendData sends messages on it and
// SendControl any control packet to the listener's socket of it.
type Raw struct {
	conn *net.UDPConn
	// ID is the caller's socket ID, a random one.
	ID uint32
	// Peer is the listener's socket ID of the connection, after Connect.
	Peer uint32
	// ISN is the first sequence number of the caller's data.
	ISN   uint32
	seq   uint32
	msg   uint32
	start time.Time
}

// DialRaw opens a UDP socket to the listener at addr, a host:port.
func DialRaw(addr string) (*Raw, error) {
	to, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		return nil, err
	}
	conn, err := net.DialUDP("udp", nil, to)
	if err != nil {
		return nil, err
	}
	id := rand.Uint32()&0x3fffffff | 1
	isn := rand.Uint32() & 0x7fffffff
	return &Raw{conn: conn, ID: id, ISN: isn, seq: isn, msg: 1, start: time.Now()}, nil
}

// LocalAddr is the address the listener sees the caller at.
func (r *Raw) LocalAddr() *net.UDPAddr {
	return r.conn.LocalAddr().(*net.UDPAddr)
}

// Close closes the socket and sends nothing, as a caller whose host is gone.
func (r *Raw) Close() error {
	return r.conn.Close()
}

// Write sends one UDP packet as it is.
func (r *Raw) Write(b []byte) error {
	_, err := r.conn.Write(b)
	return err
}

// Read reads the next packet from the listener, for at most d; a timeout
// gives an error os.ErrDeadlineExceeded wraps.
func (r *Raw) Read(d time.Duration) (packet.Packet, error) {
	b := make([]byte, 65536)
	r.conn.SetReadDeadline(time.Now().Add(d))
	n, err := r.conn.Read(b)
	if err != nil {
		return nil, err
	}
	return packet.NewPacketFromData(r.conn.RemoteAddr(), b[:n])
}

// ReadHandshake reads packets for at most d until a handshake comes, and
// returns it.
func (r *Raw) ReadHandshake(d time.Duration) (*packet.CIFHandshake, error) {
	deadline := time.Now().Add(d)
	for {
		left := time.Until(deadline)
		if left <= 0 {
			return nil, os.ErrDeadlineExceeded
		}
		p, err := r.Read(left)
		if err != nil {
			return nil, err
		}
		if !p.Header().IsControlPacket || p.Header().ControlType != packet.CTRLTYPE_HANDSHAKE {
			continue
		}
		hs := &packet.CIFHandshake{}
		if err := p.UnmarshalCIF(hs); err != nil {
			return nil, fmt.Errorf("handshake answer: %w", err)
		}
		return hs, nil
	}
}

// Induction is the caller's first handshake packet.
func (r *Raw) Induction() []byte {
	return rawHandshake(0, &packet.CIFHandshake{
		IsRequest:                   true,
		Version:                     4,
		ExtensionField:              rawInductionExt,
		InitialPacketSequenceNumber: circular.New(r.ISN, packet.MAX_SEQUENCENUMBER),
		MaxTransmissionUnitSize:     rawMTU,
		MaxFlowWindowSize:           rawFlowWindow,
		HandshakeType:               packet.HSTYPE_INDUCTION,
		SRTSocketId:                 r.ID,
	})
}

// Conclusion is the caller's second handshake packet, with the listener's
// cookie, the HSREQ of a live sender and receiver with latency, and the
// stream ID, which may be longer than MaxStreamID; empty sends none.
func (r *Raw) Conclusion(cookie uint32, streamID string, latency time.Duration) []byte {
	ms := uint16(latency.Milliseconds())
	return rawHandshake(0, &packet.CIFHandshake{
		IsRequest:                   true,
		Version:                     5,
		InitialPacketSequenceNumber: circular.New(r.ISN, packet.MAX_SEQUENCENUMBER),
		MaxTransmissionUnitSize:     rawMTU,
		MaxFlowWindowSize:           rawFlowWindow,
		HandshakeType:               packet.HSTYPE_CONCLUSION,
		SRTSocketId:                 r.ID,
		SynCookie:                   cookie,
		HasHS:                       true,
		SRTHS: &packet.CIFHandshakeExtension{
			SRTVersion: rawSRTVersion,
			SRTFlags: packet.CIFHandshakeExtensionFlags{
				TSBPDSND: true, TSBPDRCV: true, CRYPT: true, TLPKTDROP: true, PERIODICNAK: true, REXMITFLG: true,
			},
			RecvTSBPDDelay: ms,
			SendTSBPDDelay: ms,
		},
		HasSID:   streamID != "",
		StreamId: streamID,
	})
}

// Induct sends the induction and returns the listener's cookie.
func (r *Raw) Induct(d time.Duration) (uint32, error) {
	if err := r.Write(r.Induction()); err != nil {
		return 0, err
	}
	hs, err := r.ReadHandshake(d)
	if err != nil {
		return 0, fmt.Errorf("induction: %w", err)
	}
	if hs.HandshakeType != packet.HSTYPE_INDUCTION || hs.SynCookie == 0 {
		return 0, fmt.Errorf("induction answered with %v, cookie %#x", hs.HandshakeType, hs.SynCookie)
	}
	return hs.SynCookie, nil
}

// Connect makes the connection, with the stream ID and latency: the
// induction, then the conclusion. A listener that refuses it gives a
// *RejectError.
func (r *Raw) Connect(streamID string, latency, d time.Duration) error {
	cookie, err := r.Induct(d)
	if err != nil {
		return err
	}
	if err := r.Write(r.Conclusion(cookie, streamID, latency)); err != nil {
		return err
	}
	hs, err := r.ReadHandshake(d)
	if err != nil {
		return fmt.Errorf("conclusion: %w", err)
	}
	if hs.HandshakeType.IsRejection() {
		return &RejectError{Reason: int(hs.HandshakeType.Val())}
	}
	if hs.HandshakeType != packet.HSTYPE_CONCLUSION {
		return fmt.Errorf("conclusion answered with %v", hs.HandshakeType)
	}
	r.Peer = hs.SRTSocketId
	r.start = time.Now()
	return nil
}

// SendData sends one message in one data packet on the connection, the next
// sequence and message numbers, stamped with the time since Connect.
func (r *Raw) SendData(payload []byte) error {
	if err := r.Write(RawData(r.seq, r.msg, r.stamp(), r.Peer, payload)); err != nil {
		return err
	}
	r.seq = (r.seq + 1) & 0x7fffffff
	r.msg = (r.msg + 1) & 0x03ffffff
	return nil
}

// Seq is the sequence number of the next data packet SendData sends.
func (r *Raw) Seq() uint32 {
	return r.seq
}

// SendControl sends a control packet of a type, subtype and type-specific
// field, with body, to the listener's socket of the connection.
func (r *Raw) SendControl(t packet.CtrlType, subtype uint16, info uint32, body []byte) error {
	return r.Write(RawControl(t, subtype, info, r.stamp(), r.Peer, body))
}

// Shutdown ends the connection as a caller that closes does.
func (r *Raw) Shutdown() error {
	return r.SendControl(packet.CTRLTYPE_SHUTDOWN, 0, 0, make([]byte, 4))
}

func (r *Raw) stamp() uint32 {
	return uint32(time.Since(r.start).Microseconds())
}

// RawControl is a control packet: its header, and body as it is.
func RawControl(t packet.CtrlType, subtype uint16, info, stamp, dest uint32, body []byte) []byte {
	p := packet.NewPacket(nil)
	defer p.Decommission()
	h := p.Header()
	h.IsControlPacket = true
	h.ControlType = t
	h.SubType = packet.CtrlSubType(subtype)
	h.TypeSpecific = info
	h.Timestamp = stamp
	h.DestinationSocketId = dest
	p.SetData(body)
	var b bytes.Buffer
	p.Marshal(&b)
	return b.Bytes()
}

// RawData is a data packet of one whole message, in order, not encrypted.
func RawData(seq, msg, stamp, dest uint32, payload []byte) []byte {
	p := packet.NewPacket(nil)
	defer p.Decommission()
	h := p.Header()
	h.PacketSequenceNumber = circular.New(seq, packet.MAX_SEQUENCENUMBER)
	h.PacketPositionFlag = packet.SinglePacket
	h.OrderFlag = true
	h.MessageNumber = msg
	h.Timestamp = stamp
	h.DestinationSocketId = dest
	p.SetData(payload)
	var b bytes.Buffer
	p.Marshal(&b)
	return b.Bytes()
}

func rawHandshake(dest uint32, hs *packet.CIFHandshake) []byte {
	p := packet.NewPacket(nil)
	defer p.Decommission()
	h := p.Header()
	h.IsControlPacket = true
	h.ControlType = packet.CTRLTYPE_HANDSHAKE
	h.DestinationSocketId = dest
	p.MarshalCIF(hs)
	var b bytes.Buffer
	p.Marshal(&b)
	return b.Bytes()
}

// IsTimeout tells whether err is a read that timed out.
func IsTimeout(err error) bool {
	return errors.Is(err, os.ErrDeadlineExceeded)
}
