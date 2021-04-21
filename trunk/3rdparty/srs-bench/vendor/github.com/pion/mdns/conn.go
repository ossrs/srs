package mdns

import (
	"context"
	"math/big"
	"net"
	"sync"
	"time"

	"github.com/pion/logging"
	"golang.org/x/net/dns/dnsmessage"
	"golang.org/x/net/ipv4"
)

// Conn represents a mDNS Server
type Conn struct {
	mu  sync.RWMutex
	log logging.LeveledLogger

	socket  *ipv4.PacketConn
	dstAddr *net.UDPAddr

	queryInterval time.Duration
	localNames    []string
	queries       []query

	closed chan interface{}
}

type query struct {
	nameWithSuffix  string
	queryResultChan chan queryResult
}

type queryResult struct {
	answer dnsmessage.ResourceHeader
	addr   net.Addr
}

const (
	inboundBufferSize    = 512
	defaultQueryInterval = time.Second
	destinationAddress   = "224.0.0.251:5353"
	maxMessageRecords    = 3
	responseTTL          = 120
)

// Server establishes a mDNS connection over an existing conn
func Server(conn *ipv4.PacketConn, config *Config) (*Conn, error) {
	if config == nil {
		return nil, errNilConfig
	}

	ifaces, err := net.Interfaces()
	if err != nil {
		return nil, err
	}

	joinErrCount := 0
	for i := range ifaces {
		if err = conn.JoinGroup(&ifaces[i], &net.UDPAddr{IP: net.IPv4(224, 0, 0, 251)}); err != nil {
			joinErrCount++
		}
	}
	if joinErrCount >= len(ifaces) {
		return nil, errJoiningMulticastGroup
	}

	dstAddr, err := net.ResolveUDPAddr("udp", destinationAddress)
	if err != nil {
		return nil, err

	}

	loggerFactory := config.LoggerFactory
	if loggerFactory == nil {
		loggerFactory = logging.NewDefaultLoggerFactory()
	}

	localNames := []string{}
	for _, l := range config.LocalNames {
		localNames = append(localNames, l+".")
	}

	c := &Conn{
		queryInterval: defaultQueryInterval,
		queries:       []query{},
		socket:        conn,
		dstAddr:       dstAddr,
		localNames:    localNames,
		log:           loggerFactory.NewLogger("mdns"),
		closed:        make(chan interface{}),
	}
	if config.QueryInterval != 0 {
		c.queryInterval = config.QueryInterval
	}

	go c.start()
	return c, nil
}

// Close closes the mDNS Conn
func (c *Conn) Close() error {
	select {
	case <-c.closed:
		return nil
	default:
	}

	if err := c.socket.Close(); err != nil {
		return err
	}

	<-c.closed
	return nil
}

// Query sends mDNS Queries for the following name until
// either the Context is canceled/expires or we get a result
func (c *Conn) Query(ctx context.Context, name string) (dnsmessage.ResourceHeader, net.Addr, error) {
	select {
	case <-c.closed:
		return dnsmessage.ResourceHeader{}, nil, errConnectionClosed
	default:
	}

	nameWithSuffix := name + "."

	queryChan := make(chan queryResult, 1)
	c.mu.Lock()
	c.queries = append(c.queries, query{nameWithSuffix, queryChan})
	ticker := time.NewTicker(c.queryInterval)
	c.mu.Unlock()

	c.sendQuestion(nameWithSuffix)
	for {
		select {
		case <-ticker.C:
			c.sendQuestion(nameWithSuffix)
		case <-c.closed:
			return dnsmessage.ResourceHeader{}, nil, errConnectionClosed
		case res := <-queryChan:
			return res.answer, res.addr, nil
		case <-ctx.Done():
			return dnsmessage.ResourceHeader{}, nil, errContextElapsed
		}
	}
}

func ipToBytes(ip net.IP) (out [4]byte) {
	rawIP := ip.To4()
	if rawIP == nil {
		return
	}

	ipInt := big.NewInt(0)
	ipInt.SetBytes(rawIP)
	copy(out[:], ipInt.Bytes())
	return
}

func interfaceForRemote(remote string) (net.IP, error) {
	conn, err := net.Dial("udp", remote)
	if err != nil {
		return nil, err
	}

	localAddr := conn.LocalAddr().(*net.UDPAddr)
	if err := conn.Close(); err != nil {
		return nil, err
	}

	return localAddr.IP, nil
}

func (c *Conn) sendQuestion(name string) {
	packedName, err := dnsmessage.NewName(name)
	if err != nil {
		c.log.Warnf("Failed to construct mDNS packet %v", err)
		return
	}

	msg := dnsmessage.Message{
		Header: dnsmessage.Header{},
		Questions: []dnsmessage.Question{
			{
				Type:  dnsmessage.TypeA,
				Class: dnsmessage.ClassINET,
				Name:  packedName,
			},
		},
	}

	rawQuery, err := msg.Pack()
	if err != nil {
		c.log.Warnf("Failed to construct mDNS packet %v", err)
		return
	}

	if _, err := c.socket.WriteTo(rawQuery, nil, c.dstAddr); err != nil {
		c.log.Warnf("Failed to send mDNS packet %v", err)
		return
	}
}

func (c *Conn) sendAnswer(name string, dst net.IP) {
	packedName, err := dnsmessage.NewName(name)
	if err != nil {
		c.log.Warnf("Failed to construct mDNS packet %v", err)
		return
	}

	msg := dnsmessage.Message{
		Header: dnsmessage.Header{
			Response:      true,
			Authoritative: true,
		},
		Answers: []dnsmessage.Resource{
			{
				Header: dnsmessage.ResourceHeader{
					Type:  dnsmessage.TypeA,
					Class: dnsmessage.ClassINET,
					Name:  packedName,
					TTL:   responseTTL,
				},
				Body: &dnsmessage.AResource{
					A: ipToBytes(dst),
				},
			},
		},
	}

	rawAnswer, err := msg.Pack()
	if err != nil {
		c.log.Warnf("Failed to construct mDNS packet %v", err)
		return
	}

	if _, err := c.socket.WriteTo(rawAnswer, nil, c.dstAddr); err != nil {
		c.log.Warnf("Failed to send mDNS packet %v", err)
		return
	}
}

func (c *Conn) start() {
	defer func() {
		c.mu.Lock()
		defer c.mu.Unlock()
		close(c.closed)
	}()

	b := make([]byte, inboundBufferSize)
	p := dnsmessage.Parser{}

	for {
		n, _, src, err := c.socket.ReadFrom(b)
		if err != nil {
			return
		}

		func() {
			c.mu.RLock()
			defer c.mu.RUnlock()

			if _, err := p.Start(b[:n]); err != nil {
				c.log.Warnf("Failed to parse mDNS packet %v", err)
				return
			}

			for i := 0; i <= maxMessageRecords; i++ {
				q, err := p.Question()
				if err == dnsmessage.ErrSectionDone {
					break
				} else if err != nil {
					c.log.Warnf("Failed to parse mDNS packet %v", err)
					return
				}

				for _, localName := range c.localNames {
					if localName == q.Name.String() {

						localAddress, err := interfaceForRemote(src.String())
						if err != nil {
							c.log.Warnf("Failed to get local interface to communicate with %s: %v", src.String(), err)
							continue
						}

						c.sendAnswer(q.Name.String(), localAddress)
					}
				}
			}

			for i := 0; i <= maxMessageRecords; i++ {
				a, err := p.AnswerHeader()
				if err == dnsmessage.ErrSectionDone {
					return
				}
				if err != nil {
					c.log.Warnf("Failed to parse mDNS packet %v", err)
					return
				}

				if a.Type != dnsmessage.TypeA && a.Type != dnsmessage.TypeAAAA {
					continue
				}

				for i := len(c.queries) - 1; i >= 0; i-- {
					if c.queries[i].nameWithSuffix == a.Name.String() {
						c.queries[i].queryResultChan <- queryResult{a, src}
						c.queries = append(c.queries[:i], c.queries[i+1:]...)
					}
				}
			}
		}()
	}
}
