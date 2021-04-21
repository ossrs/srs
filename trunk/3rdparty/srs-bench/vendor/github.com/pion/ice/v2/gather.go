package ice

import (
	"context"
	"crypto/tls"
	"errors"
	"fmt"
	"net"
	"reflect"
	"sync"
	"time"

	"github.com/pion/dtls/v2"
	"github.com/pion/logging"
	"github.com/pion/turn/v2"
)

const (
	stunGatherTimeout = time.Second * 5
)

type closeable interface {
	Close() error
}

// Close a net.Conn and log if we have a failure
func closeConnAndLog(c closeable, log logging.LeveledLogger, msg string) {
	if c == nil || (reflect.ValueOf(c).Kind() == reflect.Ptr && reflect.ValueOf(c).IsNil()) {
		log.Warnf("Conn is not allocated (%s)", msg)
		return
	}

	log.Warnf(msg)
	if err := c.Close(); err != nil {
		log.Warnf("Failed to close conn: %v", err)
	}
}

// fakePacketConn wraps a net.Conn and emulates net.PacketConn
type fakePacketConn struct {
	nextConn net.Conn
}

func (f *fakePacketConn) ReadFrom(p []byte) (n int, addr net.Addr, err error) {
	n, err = f.nextConn.Read(p)
	addr = f.nextConn.RemoteAddr()
	return
}
func (f *fakePacketConn) Close() error                       { return f.nextConn.Close() }
func (f *fakePacketConn) LocalAddr() net.Addr                { return f.nextConn.LocalAddr() }
func (f *fakePacketConn) SetDeadline(t time.Time) error      { return f.nextConn.SetDeadline(t) }
func (f *fakePacketConn) SetReadDeadline(t time.Time) error  { return f.nextConn.SetReadDeadline(t) }
func (f *fakePacketConn) SetWriteDeadline(t time.Time) error { return f.nextConn.SetWriteDeadline(t) }
func (f *fakePacketConn) WriteTo(p []byte, addr net.Addr) (n int, err error) {
	return f.nextConn.Write(p)
}

// GatherCandidates initiates the trickle based gathering process.
func (a *Agent) GatherCandidates() error {
	var gatherErr error

	if runErr := a.run(a.context(), func(ctx context.Context, agent *Agent) {
		if a.gatheringState != GatheringStateNew {
			gatherErr = ErrMultipleGatherAttempted
			return
		} else if a.onCandidateHdlr.Load() == nil {
			gatherErr = ErrNoOnCandidateHandler
			return
		}

		a.gatherCandidateCancel() // Cancel previous gathering routine
		ctx, cancel := context.WithCancel(ctx)
		a.gatherCandidateCancel = cancel

		go a.gatherCandidates(ctx)
	}); runErr != nil {
		return runErr
	}
	return gatherErr
}

func (a *Agent) gatherCandidates(ctx context.Context) {
	if err := a.setGatheringState(GatheringStateGathering); err != nil {
		a.log.Warnf("failed to set gatheringState to GatheringStateGathering: %v", err)
		return
	}

	var wg sync.WaitGroup
	for _, t := range a.candidateTypes {
		switch t {
		case CandidateTypeHost:
			wg.Add(1)
			go func() {
				a.gatherCandidatesLocal(ctx, a.networkTypes)
				wg.Done()
			}()
		case CandidateTypeServerReflexive:
			wg.Add(1)
			go func() {
				a.gatherCandidatesSrflx(ctx, a.urls, a.networkTypes)
				wg.Done()
			}()
			if a.extIPMapper != nil && a.extIPMapper.candidateType == CandidateTypeServerReflexive {
				wg.Add(1)
				go func() {
					a.gatherCandidatesSrflxMapped(ctx, a.networkTypes)
					wg.Done()
				}()
			}
		case CandidateTypeRelay:
			wg.Add(1)
			go func() {
				a.gatherCandidatesRelay(ctx, a.urls)
				wg.Done()
			}()
		case CandidateTypePeerReflexive, CandidateTypeUnspecified:
		}
	}
	// Block until all STUN and TURN URLs have been gathered (or timed out)
	wg.Wait()

	if err := a.setGatheringState(GatheringStateComplete); err != nil {
		a.log.Warnf("failed to set gatheringState to GatheringStateComplete: %v", err)
	}
}

func (a *Agent) gatherCandidatesLocal(ctx context.Context, networkTypes []NetworkType) { //nolint:gocognit
	networks := map[string]struct{}{}
	for _, networkType := range networkTypes {
		if networkType.IsTCP() {
			networks[tcp] = struct{}{}
		} else {
			networks[udp] = struct{}{}
		}
	}

	localIPs, err := localInterfaces(a.net, a.interfaceFilter, networkTypes)
	if err != nil {
		a.log.Warnf("failed to iterate local interfaces, host candidates will not be gathered %s", err)
		return
	}

	for _, ip := range localIPs {
		mappedIP := ip
		if a.mDNSMode != MulticastDNSModeQueryAndGather && a.extIPMapper != nil && a.extIPMapper.candidateType == CandidateTypeHost {
			if _mappedIP, err := a.extIPMapper.findExternalIP(ip.String()); err == nil {
				mappedIP = _mappedIP
			} else {
				a.log.Warnf("1:1 NAT mapping is enabled but no external IP is found for %s\n", ip.String())
			}
		}

		address := mappedIP.String()
		if a.mDNSMode == MulticastDNSModeQueryAndGather {
			address = a.mDNSName
		}

		for network := range networks {
			var port int
			var conn net.PacketConn
			var err error

			var tcpType TCPType
			switch network {
			case tcp:
				// Handle ICE TCP passive mode

				a.log.Debugf("GetConn by ufrag: %s\n", a.localUfrag)
				conn, err = a.tcpMux.GetConnByUfrag(a.localUfrag)
				if err != nil {
					if !errors.Is(err, ErrTCPMuxNotInitialized) {
						a.log.Warnf("error getting tcp conn by ufrag: %s %s %s\n", network, ip, a.localUfrag)
					}
					continue
				}
				port = conn.LocalAddr().(*net.TCPAddr).Port
				tcpType = TCPTypePassive
				// is there a way to verify that the listen address is even
				// accessible from the current interface.
			case udp:
				conn, err = listenUDPInPortRange(a.net, a.log, int(a.portmax), int(a.portmin), network, &net.UDPAddr{IP: ip, Port: 0})
				if err != nil {
					a.log.Warnf("could not listen %s %s\n", network, ip)
					continue
				}

				port = conn.LocalAddr().(*net.UDPAddr).Port
			}
			hostConfig := CandidateHostConfig{
				Network:   network,
				Address:   address,
				Port:      port,
				Component: ComponentRTP,
				TCPType:   tcpType,
			}

			c, err := NewCandidateHost(&hostConfig)
			if err != nil {
				closeConnAndLog(conn, a.log, fmt.Sprintf("Failed to create host candidate: %s %s %d: %v\n", network, mappedIP, port, err))
				continue
			}

			if a.mDNSMode == MulticastDNSModeQueryAndGather {
				if err = c.setIP(ip); err != nil {
					closeConnAndLog(conn, a.log, fmt.Sprintf("Failed to create host candidate: %s %s %d: %v\n", network, mappedIP, port, err))
					continue
				}
			}

			if err := a.addCandidate(ctx, c, conn); err != nil {
				if closeErr := c.close(); closeErr != nil {
					a.log.Warnf("Failed to close candidate: %v", closeErr)
				}
				a.log.Warnf("Failed to append to localCandidates and run onCandidateHdlr: %v\n", err)
			}
		}
	}
}

func (a *Agent) gatherCandidatesSrflxMapped(ctx context.Context, networkTypes []NetworkType) {
	var wg sync.WaitGroup
	defer wg.Wait()

	for _, networkType := range networkTypes {
		if networkType.IsTCP() {
			continue
		}

		network := networkType.String()
		wg.Add(1)
		go func() {
			defer wg.Done()
			conn, err := listenUDPInPortRange(a.net, a.log, int(a.portmax), int(a.portmin), network, &net.UDPAddr{IP: nil, Port: 0})
			if err != nil {
				a.log.Warnf("Failed to listen %s: %v\n", network, err)
				return
			}

			laddr := conn.LocalAddr().(*net.UDPAddr)
			mappedIP, err := a.extIPMapper.findExternalIP(laddr.IP.String())
			if err != nil {
				closeConnAndLog(conn, a.log, fmt.Sprintf("1:1 NAT mapping is enabled but no external IP is found for %s\n", laddr.IP.String()))
				return
			}

			srflxConfig := CandidateServerReflexiveConfig{
				Network:   network,
				Address:   mappedIP.String(),
				Port:      laddr.Port,
				Component: ComponentRTP,
				RelAddr:   laddr.IP.String(),
				RelPort:   laddr.Port,
			}
			c, err := NewCandidateServerReflexive(&srflxConfig)
			if err != nil {
				closeConnAndLog(conn, a.log, fmt.Sprintf("Failed to create server reflexive candidate: %s %s %d: %v\n",
					network,
					mappedIP.String(),
					laddr.Port,
					err))
				return
			}

			if err := a.addCandidate(ctx, c, conn); err != nil {
				if closeErr := c.close(); closeErr != nil {
					a.log.Warnf("Failed to close candidate: %v", closeErr)
				}
				a.log.Warnf("Failed to append to localCandidates and run onCandidateHdlr: %v\n", err)
			}
		}()
	}
}

func (a *Agent) gatherCandidatesSrflx(ctx context.Context, urls []*URL, networkTypes []NetworkType) {
	var wg sync.WaitGroup
	defer wg.Wait()

	for _, networkType := range networkTypes {
		if networkType.IsTCP() {
			continue
		}

		for i := range urls {
			wg.Add(1)
			go func(url URL, network string) {
				defer wg.Done()
				hostPort := fmt.Sprintf("%s:%d", url.Host, url.Port)
				serverAddr, err := a.net.ResolveUDPAddr(network, hostPort)
				if err != nil {
					a.log.Warnf("failed to resolve stun host: %s: %v", hostPort, err)
					return
				}

				conn, err := listenUDPInPortRange(a.net, a.log, int(a.portmax), int(a.portmin), network, &net.UDPAddr{IP: nil, Port: 0})
				if err != nil {
					closeConnAndLog(conn, a.log, fmt.Sprintf("Failed to listen for %s: %v\n", serverAddr.String(), err))
					return
				}

				xoraddr, err := getXORMappedAddr(conn, serverAddr, stunGatherTimeout)
				if err != nil {
					closeConnAndLog(conn, a.log, fmt.Sprintf("could not get server reflexive address %s %s: %v\n", network, url, err))
					return
				}

				ip := xoraddr.IP
				port := xoraddr.Port

				laddr := conn.LocalAddr().(*net.UDPAddr)
				srflxConfig := CandidateServerReflexiveConfig{
					Network:   network,
					Address:   ip.String(),
					Port:      port,
					Component: ComponentRTP,
					RelAddr:   laddr.IP.String(),
					RelPort:   laddr.Port,
				}
				c, err := NewCandidateServerReflexive(&srflxConfig)
				if err != nil {
					closeConnAndLog(conn, a.log, fmt.Sprintf("Failed to create server reflexive candidate: %s %s %d: %v\n", network, ip, port, err))
					return
				}

				if err := a.addCandidate(ctx, c, conn); err != nil {
					if closeErr := c.close(); closeErr != nil {
						a.log.Warnf("Failed to close candidate: %v", closeErr)
					}
					a.log.Warnf("Failed to append to localCandidates and run onCandidateHdlr: %v\n", err)
				}
			}(*urls[i], networkType.String())
		}
	}
}

func (a *Agent) gatherCandidatesRelay(ctx context.Context, urls []*URL) { //nolint:gocognit
	var wg sync.WaitGroup
	defer wg.Wait()

	network := NetworkTypeUDP4.String()
	for i := range urls {
		switch {
		case urls[i].Scheme != SchemeTypeTURN && urls[i].Scheme != SchemeTypeTURNS:
			continue
		case urls[i].Username == "":
			a.log.Errorf("Failed to gather relay candidates: %v", ErrUsernameEmpty)
			return
		case urls[i].Password == "":
			a.log.Errorf("Failed to gather relay candidates: %v", ErrPasswordEmpty)
			return
		}

		wg.Add(1)
		go func(url URL) {
			defer wg.Done()
			TURNServerAddr := fmt.Sprintf("%s:%d", url.Host, url.Port)
			var (
				locConn net.PacketConn
				err     error
				RelAddr string
				RelPort int
			)

			switch {
			case url.Proto == ProtoTypeUDP && url.Scheme == SchemeTypeTURN:
				if locConn, err = a.net.ListenPacket(network, "0.0.0.0:0"); err != nil {
					a.log.Warnf("Failed to listen %s: %v\n", network, err)
					return
				}

				RelAddr = locConn.LocalAddr().(*net.UDPAddr).IP.String()
				RelPort = locConn.LocalAddr().(*net.UDPAddr).Port
			case a.proxyDialer != nil && url.Proto == ProtoTypeTCP &&
				(url.Scheme == SchemeTypeTURN || url.Scheme == SchemeTypeTURNS):
				conn, connectErr := a.proxyDialer.Dial(NetworkTypeTCP4.String(), TURNServerAddr)
				if connectErr != nil {
					a.log.Warnf("Failed to Dial TCP Addr %s via proxy dialer: %v\n", TURNServerAddr, connectErr)
					return
				}

				RelAddr = conn.LocalAddr().(*net.TCPAddr).IP.String()
				RelPort = conn.LocalAddr().(*net.TCPAddr).Port
				locConn = turn.NewSTUNConn(conn)

			case url.Proto == ProtoTypeTCP && url.Scheme == SchemeTypeTURN:
				tcpAddr, connectErr := net.ResolveTCPAddr(NetworkTypeTCP4.String(), TURNServerAddr)
				if connectErr != nil {
					a.log.Warnf("Failed to resolve TCP Addr %s: %v\n", TURNServerAddr, connectErr)
					return
				}

				conn, connectErr := net.DialTCP(NetworkTypeTCP4.String(), nil, tcpAddr)
				if connectErr != nil {
					a.log.Warnf("Failed to Dial TCP Addr %s: %v\n", TURNServerAddr, connectErr)
					return
				}

				RelAddr = conn.LocalAddr().(*net.TCPAddr).IP.String()
				RelPort = conn.LocalAddr().(*net.TCPAddr).Port
				locConn = turn.NewSTUNConn(conn)
			case url.Proto == ProtoTypeUDP && url.Scheme == SchemeTypeTURNS:
				udpAddr, connectErr := net.ResolveUDPAddr(network, TURNServerAddr)
				if connectErr != nil {
					a.log.Warnf("Failed to resolve UDP Addr %s: %v\n", TURNServerAddr, connectErr)
					return
				}

				conn, connectErr := dtls.Dial(network, udpAddr, &dtls.Config{
					InsecureSkipVerify: a.insecureSkipVerify, //nolint:gosec
				})
				if connectErr != nil {
					a.log.Warnf("Failed to Dial DTLS Addr %s: %v\n", TURNServerAddr, connectErr)
					return
				}

				RelAddr = conn.LocalAddr().(*net.UDPAddr).IP.String()
				RelPort = conn.LocalAddr().(*net.UDPAddr).Port
				locConn = &fakePacketConn{conn}
			case url.Proto == ProtoTypeTCP && url.Scheme == SchemeTypeTURNS:
				conn, connectErr := tls.Dial(NetworkTypeTCP4.String(), TURNServerAddr, &tls.Config{
					InsecureSkipVerify: a.insecureSkipVerify, //nolint:gosec
				})
				if connectErr != nil {
					a.log.Warnf("Failed to Dial TLS Addr %s: %v\n", TURNServerAddr, connectErr)
					return
				}
				RelAddr = conn.LocalAddr().(*net.TCPAddr).IP.String()
				RelPort = conn.LocalAddr().(*net.TCPAddr).Port
				locConn = turn.NewSTUNConn(conn)
			default:
				a.log.Warnf("Unable to handle URL in gatherCandidatesRelay %v\n", url)
				return
			}

			client, err := turn.NewClient(&turn.ClientConfig{
				TURNServerAddr: TURNServerAddr,
				Conn:           locConn,
				Username:       url.Username,
				Password:       url.Password,
				LoggerFactory:  a.loggerFactory,
				Net:            a.net,
			})
			if err != nil {
				closeConnAndLog(locConn, a.log, fmt.Sprintf("Failed to build new turn.Client %s %s\n", TURNServerAddr, err))
				return
			}

			if err = client.Listen(); err != nil {
				client.Close()
				closeConnAndLog(locConn, a.log, fmt.Sprintf("Failed to listen on turn.Client %s %s\n", TURNServerAddr, err))
				return
			}

			relayConn, err := client.Allocate()
			if err != nil {
				client.Close()
				closeConnAndLog(locConn, a.log, fmt.Sprintf("Failed to allocate on turn.Client %s %s\n", TURNServerAddr, err))
				return
			}

			raddr := relayConn.LocalAddr().(*net.UDPAddr)
			relayConfig := CandidateRelayConfig{
				Network:   network,
				Component: ComponentRTP,
				Address:   raddr.IP.String(),
				Port:      raddr.Port,
				RelAddr:   RelAddr,
				RelPort:   RelPort,
				OnClose: func() error {
					client.Close()
					return locConn.Close()
				},
			}
			relayConnClose := func() {
				if relayConErr := relayConn.Close(); relayConErr != nil {
					a.log.Warnf("Failed to close relay %v", relayConErr)
				}
			}
			candidate, err := NewCandidateRelay(&relayConfig)
			if err != nil {
				relayConnClose()

				client.Close()
				closeConnAndLog(locConn, a.log, fmt.Sprintf("Failed to create relay candidate: %s %s: %v\n", network, raddr.String(), err))
				return
			}

			if err := a.addCandidate(ctx, candidate, relayConn); err != nil {
				relayConnClose()

				if closeErr := candidate.close(); closeErr != nil {
					a.log.Warnf("Failed to close candidate: %v", closeErr)
				}
				a.log.Warnf("Failed to append to localCandidates and run onCandidateHdlr: %v\n", err)
			}
		}(*urls[i])
	}
}
