package gortsplib

import (
	"crypto/rand"
	"math/big"
	"net"
	"strconv"
	"sync/atomic"
	"time"

	"github.com/bluenviron/gortsplib/v4/pkg/multicast"
)

func int64Ptr(v int64) *int64 {
	return &v
}

func randInRange(maxVal int) (int, error) {
	b := big.NewInt(int64(maxVal + 1))
	n, err := rand.Int(rand.Reader, b)
	if err != nil {
		return 0, err
	}
	return int(n.Int64()), nil
}

func createUDPListenerPair(c *Client) (*clientUDPListener, *clientUDPListener, error) {
	// choose two consecutive ports in range 65535-10000
	// RTP port must be even and RTCP port odd
	for {
		v, err := randInRange((65535 - 10000) / 2)
		if err != nil {
			return nil, nil, err
		}

		rtpPort := v*2 + 10000
		rtcpPort := rtpPort + 1

		rtpListener := &clientUDPListener{
			c:                 c,
			multicastEnable:   false,
			multicastSourceIP: nil,
			address:           net.JoinHostPort("", strconv.FormatInt(int64(rtpPort), 10)),
		}
		err = rtpListener.initialize()
		if err != nil {
			continue
		}

		rtcpListener := &clientUDPListener{
			c:                 c,
			multicastEnable:   false,
			multicastSourceIP: nil,
			address:           net.JoinHostPort("", strconv.FormatInt(int64(rtcpPort), 10)),
		}
		err = rtcpListener.initialize()
		if err != nil {
			rtpListener.close()
			continue
		}

		return rtpListener, rtcpListener, nil
	}
}

type packetConn interface {
	net.PacketConn
	SetReadBuffer(int) error
}

type clientUDPListener struct {
	c                 *Client
	multicastEnable   bool
	multicastSourceIP net.IP
	address           string

	pc        packetConn
	readFunc  readFunc
	readIP    net.IP
	readPort  int
	writeAddr *net.UDPAddr

	running        bool
	lastPacketTime *int64

	done chan struct{}
}

func (u *clientUDPListener) initialize() error {
	if u.multicastEnable {
		intf, err := multicast.InterfaceForSource(u.multicastSourceIP)
		if err != nil {
			return err
		}

		u.pc, err = multicast.NewSingleConn(intf, u.address, u.c.ListenPacket)
		if err != nil {
			return err
		}
	} else {
		tmp, err := u.c.ListenPacket(restrictNetwork("udp", u.address))
		if err != nil {
			return err
		}
		u.pc = tmp.(*net.UDPConn)
	}

	err := u.pc.SetReadBuffer(udpKernelReadBufferSize)
	if err != nil {
		u.pc.Close()
		return err
	}

	u.lastPacketTime = int64Ptr(0)
	return nil
}

func (u *clientUDPListener) close() {
	if u.running {
		u.stop()
	}
	u.pc.Close()
}

func (u *clientUDPListener) port() int {
	return u.pc.LocalAddr().(*net.UDPAddr).Port
}

func (u *clientUDPListener) start() {
	u.running = true
	u.pc.SetReadDeadline(time.Time{})
	u.done = make(chan struct{})
	go u.run()
}

func (u *clientUDPListener) stop() {
	u.pc.SetReadDeadline(time.Now())
	<-u.done
	u.running = false
}

func (u *clientUDPListener) run() {
	defer close(u.done)

	var buf []byte

	createNewBuffer := func() {
		buf = make([]byte, udpMaxPayloadSize+1)
	}

	createNewBuffer()

	for {
		n, addr, err := u.pc.ReadFrom(buf)
		if err != nil {
			return
		}

		uaddr := addr.(*net.UDPAddr)

		if !u.readIP.Equal(uaddr.IP) {
			continue
		}

		// in case of anyPortEnable, store the port of the first packet we receive.
		// this reduces security issues
		if u.c.AnyPortEnable && u.readPort == 0 {
			u.readPort = uaddr.Port
		} else if u.readPort != uaddr.Port {
			continue
		}

		now := u.c.timeNow()
		atomic.StoreInt64(u.lastPacketTime, now.Unix())

		if u.readFunc(buf[:n]) {
			createNewBuffer()
		}
	}
}

func (u *clientUDPListener) write(payload []byte) error {
	// no mutex is needed here since Write() has an internal lock.
	// https://github.com/golang/go/issues/27203#issuecomment-534386117
	u.pc.SetWriteDeadline(time.Now().Add(u.c.WriteTimeout))
	_, err := u.pc.WriteTo(payload, u.writeAddr)
	return err
}
