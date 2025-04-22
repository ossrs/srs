package gortsplib

import (
	"net"
	"strconv"
	"sync"
	"time"

	"github.com/bluenviron/gortsplib/v4/pkg/multicast"
)

type clientAddr struct {
	ip   [net.IPv6len]byte // use a fixed-size array to enable the equality operator
	port int
}

func (p *clientAddr) fill(ip net.IP, port int) {
	p.port = port

	if len(ip) == net.IPv4len {
		copy(p.ip[0:], []byte{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff}) // v4InV6Prefix
		copy(p.ip[12:], ip)
	} else {
		copy(p.ip[:], ip)
	}
}

func createUDPListenerMulticastPair(
	listenPacket func(network, address string) (net.PacketConn, error),
	writeTimeout time.Duration,
	multicastRTPPort int,
	multicastRTCPPort int,
	ip net.IP,
) (*serverUDPListener, *serverUDPListener, error) {
	rtpl := &serverUDPListener{
		listenPacket:    listenPacket,
		writeTimeout:    writeTimeout,
		multicastEnable: true,
		address:         net.JoinHostPort(ip.String(), strconv.FormatInt(int64(multicastRTPPort), 10)),
	}
	err := rtpl.initialize()
	if err != nil {
		return nil, nil, err
	}

	rtcpl := &serverUDPListener{
		listenPacket:    listenPacket,
		writeTimeout:    writeTimeout,
		multicastEnable: true,
		address:         net.JoinHostPort(ip.String(), strconv.FormatInt(int64(multicastRTCPPort), 10)),
	}
	err = rtcpl.initialize()
	if err != nil {
		rtpl.close()
		return nil, nil, err
	}

	return rtpl, rtcpl, nil
}

type serverUDPListener struct {
	listenPacket    func(network, address string) (net.PacketConn, error)
	writeTimeout    time.Duration
	multicastEnable bool
	address         string

	pc           packetConn
	listenIP     net.IP
	clientsMutex sync.RWMutex
	clients      map[clientAddr]readFunc

	done chan struct{}
}

func (u *serverUDPListener) initialize() error {
	if u.multicastEnable {
		var err error
		u.pc, err = multicast.NewMultiConn(u.address, false, u.listenPacket)
		if err != nil {
			return err
		}

		host, _, err := net.SplitHostPort(u.address)
		if err != nil {
			return err
		}
		u.listenIP = net.ParseIP(host)
	} else {
		tmp, err := u.listenPacket(restrictNetwork("udp", u.address))
		if err != nil {
			return err
		}
		u.pc = tmp.(*net.UDPConn)
		u.listenIP = tmp.LocalAddr().(*net.UDPAddr).IP
	}

	err := u.pc.SetReadBuffer(udpKernelReadBufferSize)
	if err != nil {
		u.pc.Close()
		return err
	}

	u.clients = make(map[clientAddr]readFunc)
	u.done = make(chan struct{})

	go u.run()

	return nil
}

func (u *serverUDPListener) close() {
	u.pc.Close()
	<-u.done
}

func (u *serverUDPListener) ip() net.IP {
	return u.listenIP
}

func (u *serverUDPListener) port() int {
	return u.pc.LocalAddr().(*net.UDPAddr).Port
}

func (u *serverUDPListener) run() {
	defer close(u.done)

	var buf []byte

	createNewBuffer := func() {
		buf = make([]byte, udpMaxPayloadSize+1)
	}

	createNewBuffer()

	for {
		n, addr2, err := u.pc.ReadFrom(buf)
		if err != nil {
			break
		}
		addr := addr2.(*net.UDPAddr)

		func() {
			u.clientsMutex.RLock()
			defer u.clientsMutex.RUnlock()

			var ca clientAddr
			ca.fill(addr.IP, addr.Port)
			cb, ok := u.clients[ca]
			if !ok {
				return
			}

			if cb(buf[:n]) {
				createNewBuffer()
			}
		}()
	}
}

func (u *serverUDPListener) write(buf []byte, addr *net.UDPAddr) error {
	// no mutex is needed here since Write() has an internal lock.
	// https://github.com/golang/go/issues/27203#issuecomment-534386117
	u.pc.SetWriteDeadline(time.Now().Add(u.writeTimeout))
	_, err := u.pc.WriteTo(buf, addr)
	return err
}

func (u *serverUDPListener) addClient(ip net.IP, port int, cb readFunc) {
	var addr clientAddr
	addr.fill(ip, port)

	u.clientsMutex.Lock()
	defer u.clientsMutex.Unlock()

	u.clients[addr] = cb
}

func (u *serverUDPListener) removeClient(ip net.IP, port int) {
	var addr clientAddr
	addr.fill(ip, port)

	u.clientsMutex.Lock()
	defer u.clientsMutex.Unlock()

	delete(u.clients, addr)
}
