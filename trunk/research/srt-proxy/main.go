package main

import (
	"context"
	"encoding/binary"
	"fmt"
	"net"
	"os"
	"time"
)

// Use FFmpeg to push stream to this proxy:
//  	ffmpeg -re -i ~/git/srs/trunk/doc/source.flv -c copy -pes_payload_size 0 -f mpegts 'srt://localhost:10081?streamid=#!::r=live/livestream?m=publish'
// Play by SRT from this proxy:
//		ffplay 'srt://localhost:10081?streamid=#!::r=live/livestream,latency=20,m=request'
var listenAddress = "127.0.0.1:10081"

// Proxy to backend SRS Server.
// Play by HTTP-FLV from SRS:
//		ffplay http://localhost:8080/live/livestream.flv
// Play by SRT from SRS:
//		ffplay 'srt://localhost:10080?streamid=#!::r=live/livestream,latency=20,m=request'
var backendAddress = "127.0.0.1:10080"

func main() {
	fmt.Println("Hello, SRT!")
	if err := doMain(context.Background()); err != nil {
		fmt.Println(fmt.Sprintf("err %+v", err))
		os.Exit(1)
	}
}

func doMain(ctx context.Context) error {
	serverAddr, err := net.ResolveUDPAddr("udp", listenAddress)
	if err != nil {
		return err
	}

	server, err := net.ListenUDP("udp", serverAddr)
	if err != nil {
		return err
	}
	defer server.Close()
	fmt.Println("UDP server listening on", server.LocalAddr().String())

	start := time.Now()

	buf := make([]byte, 4096)
	connections := make(map[string]*SRTConnection)
	for {
		n, clientAddr, err := server.ReadFromUDP(buf)
		if err != nil {
			return err
		}

		connection, ok := connections[clientAddr.String()]
		if !ok {
			connection = &SRTConnection{
				start:      start,
				server:     server,
				serverAddr: serverAddr,
				clientAddr: clientAddr,
			}
			connections[clientAddr.String()] = connection
			fmt.Println("New connection from", clientAddr.String())
		}

		if err := connection.Consume(buf[:n]); err != nil {
			return err
		}

		fmt.Println(fmt.Sprintf("Received %v bytes from %s", n, clientAddr.String()))
	}

	return nil
}

type SRTConnection struct {
	// Listener start time.
	start time.Time
	// Local UDP server connection.
	server *net.UDPConn
	// Local UDP server listen address.
	serverAddr *net.UDPAddr
	// Client remote address.
	clientAddr *net.UDPAddr
	// Backend server connection.
	backend *net.UDPConn

	// Handshake packets with client.
	handshake0 *SRTHandshakePacket
	handshake1 *SRTHandshakePacket
	handshake2 *SRTHandshakePacket
	handshake3 *SRTHandshakePacket
}

func (v *SRTConnection) Close() error {
	if v.backend != nil {
		return v.backend.Close()
	}
	return nil
}

func (v *SRTConnection) Consume(b []byte) error {
	pkt := &SRTHandshakePacket{}
	if err := pkt.UnmarshalBinary(b); err != nil {
		return err
	}

	// Handle handshake messages.
	if pkt.IsHandshake() {
		if pkt.SynCookie == 0 {
			// Save handshake packet.
			v.handshake0 = pkt
			fmt.Println(fmt.Sprintf("Handshake 0: %v", v.handshake0.String()))

			// Response handshake 1.
			v.handshake1 = &SRTHandshakePacket{
				ControlFlag:     pkt.ControlFlag,
				ControlType:     0,
				SubType:         0,
				AdditionalInfo:  0,
				Timestamp:       uint32(time.Since(v.start).Microseconds()),
				SocketID:        pkt.SRTSocketID,
				Version:         5,
				EncryptionField: 0,
				ExtensionField:  0x4A17,
				InitSequence:    pkt.InitSequence,
				MTU:             pkt.MTU,
				FlowWindow:      pkt.FlowWindow,
				HandshakeType:   1,
				SRTSocketID:     pkt.SRTSocketID,
				SynCookie:       0x418d5e4e,
				PeerIP:          v.serverAddr.IP,
			}
			fmt.Println(fmt.Sprintf("Handshake 1: %v", v.handshake1.String()))

			if b, err := v.handshake1.MarshalBinary(); err != nil {
				return err
			} else if _, err = v.server.WriteToUDP(b, v.clientAddr); err != nil {
				return err
			}

			return nil
		} else {
			// Save handshake packet.
			v.handshake2 = pkt
			fmt.Println(fmt.Sprintf("Handshake 2: %v", v.handshake2.String()))

			// Ignore if already connected.
			if v.backend == nil {
				remoteAddress, err := net.ResolveUDPAddr("udp", backendAddress)
				if err != nil {
					return err
				}

				if v.backend, err = net.DialUDP("udp", nil, remoteAddress); err != nil {
					return err
				}
			}

			// Proxy handshake 0 to backend server.
			if b, err := v.handshake0.MarshalBinary(); err != nil {
				return err
			} else if _, err = v.backend.Write(b); err != nil {
				return err
			}
			fmt.Println(fmt.Sprintf("Proxy send handshake 0: %v", v.handshake0.String()))

			// Read handshake 1 from backend server.
			b := make([]byte, 4096)
			handshake1p := &SRTHandshakePacket{}
			if nn, err := v.backend.Read(b); err != nil {
				return err
			} else if err := handshake1p.UnmarshalBinary(b[:nn]); err != nil {
				return err
			}
			fmt.Println(fmt.Sprintf("Proxy got handshake 1: %v", handshake1p.String()))

			// Proxy handshake 2 to backend server.
			handshake2p := *v.handshake2
			handshake2p.SynCookie = handshake1p.SynCookie
			if b, err := handshake2p.MarshalBinary(); err != nil {
				return err
			} else if _, err = v.backend.Write(b); err != nil {
				return err
			}
			fmt.Println(fmt.Sprintf("Proxy send handshake 2: %v", handshake2p.String()))

			// Read handshake 3 from backend server.
			handshake3p := &SRTHandshakePacket{}
			if nn, err := v.backend.Read(b); err != nil {
				return err
			} else if err := handshake3p.UnmarshalBinary(b[:nn]); err != nil {
				return err
			}
			fmt.Println(fmt.Sprintf("Proxy got handshake 3: %v", handshake3p.String()))

			// Response handshake 3 to client.
			v.handshake3 = &*handshake3p
			v.handshake3.SynCookie = v.handshake1.SynCookie
			fmt.Println(fmt.Sprintf("Handshake 3: %v", v.handshake3.String()))

			if b, err := v.handshake3.MarshalBinary(); err != nil {
				return err
			} else if _, err = v.server.WriteToUDP(b, v.clientAddr); err != nil {
				return err
			}

			// Start a goroutine to proxy message from backend to client.
			go func() {
				for {
					nn, err := v.backend.Read(b)
					if err != nil {
						return
					} else if _, err = v.server.WriteToUDP(b[:nn], v.clientAddr); err != nil {
						return
					}
					fmt.Println(fmt.Sprintf("Proxy got %d bytes from backend server.", nn))
				}
			}()
			return nil
		}
	}

	// Proxy all other messages to backend server.
	if _, err := v.backend.Write(b); err != nil {
		return err
	}

	fmt.Println(fmt.Sprintf("Packet: %v", pkt))
	return nil
}

// See https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-3.2
// See https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-3.2.1
type SRTHandshakePacket struct {
	// F: 1 bit.  Packet Type Flag.  The control packet has this flag set to
	//      "1".  The data packet has this flag set to "0".
	ControlFlag uint8
	// Control Type: 15 bits.  Control Packet Type.  The use of these bits
	//      is determined by the control packet type definition.
	// Handshake control packets (Control Type = 0x0000) are used to
	//   exchange peer configurations, to agree on connection parameters, and
	//   to establish a connection.
	ControlType uint16
	// Subtype: 16 bits.  This field specifies an additional subtype for
	//      specific packets.
	SubType uint16
	// Type-specific Information: 32 bits.  The use of this field depends on
	//      the particular control packet type.  Handshake packets do not use
	//      this field.
	AdditionalInfo uint32
	// Timestamp: 32 bits.
	Timestamp uint32
	// Destination Socket ID: 32 bits.
	SocketID uint32

	// Version: 32 bits.  A base protocol version number.  Currently used
	//      values are 4 and 5.  Values greater than 5 are reserved for future
	//      use.
	Version uint32
	// Encryption Field: 16 bits.  Block cipher family and key size.  The
	//      values of this field are described in Table 2.  The default value
	//      is AES-128.
	// 0     |  No Encryption Advertised
	// 2     |          AES-128
	// 3     |          AES-192
	// 4     |          AES-256
	EncryptionField uint16
	// Extension Field: 16 bits.  This field is message specific extension
	//      related to Handshake Type field.  The value MUST be set to 0
	//      except for the following cases.  (1) If the handshake control
	//      packet is the INDUCTION message, this field is sent back by the
	//      Listener. (2) In the case of a CONCLUSION message, this field
	//      value should contain a combination of Extension Type values.
	// 0x00000001 | HSREQ
	// 0x00000002 | KMREQ
	// 0x00000004 | CONFIG
	// 0x4A17 if HandshakeType is INDUCTION, see https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-4.3.1.1
	ExtensionField uint16
	// Initial Packet Sequence Number: 32 bits.  The sequence number of the
	//      very first data packet to be sent.
	InitSequence uint32
	// Maximum Transmission Unit Size: 32 bits.  This value is typically set
	//      to 1500, which is the default Maximum Transmission Unit (MTU) size
	//      for Ethernet, but can be less.
	MTU uint32
	// Maximum Flow Window Size: 32 bits.  The value of this field is the
	//      maximum number of data packets allowed to be "in flight" (i.e. the
	//      number of sent packets for which an ACK control packet has not yet
	//      been received).
	FlowWindow uint32
	// Handshake Type: 32 bits.  This field indicates the handshake packet
	//      type.
	// 0xFFFFFFFD |      DONE
	// 0xFFFFFFFE |   AGREEMENT
	// 0xFFFFFFFF |   CONCLUSION
	// 0x00000000 |    WAVEHAND
	// 0x00000001 |   INDUCTION
	HandshakeType uint32
	// SRT Socket ID: 32 bits.  This field holds the ID of the source SRT
	//      socket from which a handshake packet is issued.
	SRTSocketID uint32
	// SYN Cookie: 32 bits.  Randomized value for processing a handshake.
	//      The value of this field is specified by the handshake message
	//      type.
	SynCookie uint32
	// Peer IP Address: 128 bits.  IPv4 or IPv6 address of the packet's
	//      sender.  The value consists of four 32-bit fields.
	PeerIP net.IP
	// Extensions.
	// Extension Type: 16 bits.  The value of this field is used to process
	//      an integrated handshake.  Each extension can have a pair of
	//      request and response types.
	// Extension Length: 16 bits.  The length of the Extension Contents
	//      field in four-byte blocks.
	// Extension Contents: variable length.  The payload of the extension.
	ExtraData []byte
}

func (v *SRTHandshakePacket) IsControl() bool {
	return v.ControlFlag == 0x80
}

func (v *SRTHandshakePacket) IsHandshake() bool {
	return v.IsControl() && v.ControlType == 0x00 && v.SubType == 0x00
}

func (v *SRTHandshakePacket) String() string {
	return fmt.Sprintf("Control=%v, CType=%v, SType=%v, Timestamp=%v, SocketID=%v, Version=%v, Encrypt=%v, Extension=%v, InitSequence=%v, MTU=%v, FlowWnd=%v, HSType=%v, SRTSocketID=%v, Cookie=%v, Peer=%vB, Extra=%vB",
		v.IsControl(), v.ControlType, v.SubType, v.Timestamp, v.SocketID, v.Version, v.EncryptionField, v.ExtensionField, v.InitSequence, v.MTU, v.FlowWindow, v.HandshakeType, v.SRTSocketID, v.SynCookie, len(v.PeerIP), len(v.ExtraData))
}

func (v *SRTHandshakePacket) UnmarshalBinary(b []byte) error {
	if len(b) < 4 {
		return fmt.Errorf("Invalid packet length %v", len(b))
	}
	v.ControlFlag = b[0] & 0x80
	v.ControlType = binary.BigEndian.Uint16(b[0:2]) & 0x7fff
	v.SubType = binary.BigEndian.Uint16(b[2:4])

	if !v.IsHandshake() {
		return nil
	}

	if len(b) < 64 {
		return fmt.Errorf("Invalid packet length %v", len(b))
	}
	v.AdditionalInfo = binary.BigEndian.Uint32(b[4:])
	v.Timestamp = binary.BigEndian.Uint32(b[8:])
	v.SocketID = binary.BigEndian.Uint32(b[12:])
	v.Version = binary.BigEndian.Uint32(b[16:])
	v.EncryptionField = binary.BigEndian.Uint16(b[20:])
	v.ExtensionField = binary.BigEndian.Uint16(b[22:])
	v.InitSequence = binary.BigEndian.Uint32(b[24:])
	v.MTU = binary.BigEndian.Uint32(b[28:])
	v.FlowWindow = binary.BigEndian.Uint32(b[32:])
	v.HandshakeType = binary.BigEndian.Uint32(b[36:])
	v.SRTSocketID = binary.BigEndian.Uint32(b[40:])
	v.SynCookie = binary.BigEndian.Uint32(b[44:])

	// Only support IPv4.
	v.PeerIP = net.IPv4(b[51], b[50], b[49], b[48])

	v.ExtraData = b[64:]

	return nil
}

func (v *SRTHandshakePacket) MarshalBinary() ([]byte, error) {
	b := make([]byte, 64+len(v.ExtraData))
	binary.BigEndian.PutUint16(b, uint16(v.ControlFlag)<<8|v.ControlType)
	binary.BigEndian.PutUint16(b[2:], v.SubType)
	binary.BigEndian.PutUint32(b[4:], v.AdditionalInfo)
	binary.BigEndian.PutUint32(b[8:], v.Timestamp)
	binary.BigEndian.PutUint32(b[12:], v.SocketID)
	binary.BigEndian.PutUint32(b[16:], v.Version)
	binary.BigEndian.PutUint16(b[20:], v.EncryptionField)
	binary.BigEndian.PutUint16(b[22:], v.ExtensionField)
	binary.BigEndian.PutUint32(b[24:], v.InitSequence)
	binary.BigEndian.PutUint32(b[28:], v.MTU)
	binary.BigEndian.PutUint32(b[32:], v.FlowWindow)
	binary.BigEndian.PutUint32(b[36:], v.HandshakeType)
	binary.BigEndian.PutUint32(b[40:], v.SRTSocketID)
	binary.BigEndian.PutUint32(b[44:], v.SynCookie)

	// Only support IPv4.
	ip := v.PeerIP.To4()
	b[48] = ip[3]
	b[49] = ip[2]
	b[50] = ip[1]
	b[51] = ip[0]

	if len(v.ExtraData) > 0 {
		copy(b[64:], v.ExtraData)
	}

	return b, nil
}
