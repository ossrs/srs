// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vnet

import (
	"errors"
	"fmt"
	"math/rand"
	"net"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/logging"
	"github.com/pion/transport/v3"
)

const (
	defaultRouterQueueSize = 0 // unlimited
)

var (
	errInvalidLocalIPinStaticIPs     = errors.New("invalid local IP in StaticIPs")
	errLocalIPBeyondStaticIPsSubset  = errors.New("mapped in StaticIPs is beyond subnet")
	errLocalIPNoStaticsIPsAssociated = errors.New("all StaticIPs must have associated local IPs")
	errRouterAlreadyStarted          = errors.New("router already started")
	errRouterAlreadyStopped          = errors.New("router already stopped")
	errStaticIPisBeyondSubnet        = errors.New("static IP is beyond subnet")
	errAddressSpaceExhausted         = errors.New("address space exhausted")
	errNoIPAddrEth0                  = errors.New("no IP address is assigned for eth0")
)

// Generate a unique router name
var assignRouterName = func() func() string { //nolint:gochecknoglobals
	var routerIDCtr uint64

	return func() string {
		n := atomic.AddUint64(&routerIDCtr, 1)
		return fmt.Sprintf("router%d", n)
	}
}()

// RouterConfig ...
type RouterConfig struct {
	// Name of router. If not specified, a unique name will be assigned.
	Name string
	// CIDR notation, like "192.0.2.0/24"
	CIDR string
	// StaticIPs is an array of static IP addresses to be assigned for this router.
	// If no static IP address is given, the router will automatically assign
	// an IP address.
	// This will be ignored if this router is the root.
	StaticIPs []string
	// StaticIP is deprecated. Use StaticIPs.
	StaticIP string
	// Internal queue size
	QueueSize int
	// Effective only when this router has a parent router
	NATType *NATType
	// Minimum Delay
	MinDelay time.Duration
	// Max Jitter
	MaxJitter time.Duration
	// Logger factory
	LoggerFactory logging.LoggerFactory
}

// NIC is a network interface controller that interfaces Router
type NIC interface {
	getInterface(ifName string) (*transport.Interface, error)
	onInboundChunk(c Chunk)
	getStaticIPs() []net.IP
	setRouter(r *Router) error
}

// ChunkFilter is a handler users can add to filter chunks.
// If the filter returns false, the packet will be dropped.
type ChunkFilter func(c Chunk) bool

// Router ...
type Router struct {
	name           string                    // read-only
	interfaces     []*transport.Interface    // read-only
	ipv4Net        *net.IPNet                // read-only
	staticIPs      []net.IP                  // read-only
	staticLocalIPs map[string]net.IP         // read-only,
	lastID         byte                      // requires mutex [x], used to assign the last digit of IPv4 address
	queue          *chunkQueue               // read-only
	parent         *Router                   // read-only
	children       []*Router                 // read-only
	natType        *NATType                  // read-only
	nat            *networkAddressTranslator // read-only
	nics           map[string]NIC            // read-only
	stopFunc       func()                    // requires mutex [x]
	resolver       *resolver                 // read-only
	chunkFilters   []ChunkFilter             // requires mutex [x]
	minDelay       time.Duration             // requires mutex [x]
	maxJitter      time.Duration             // requires mutex [x]
	mutex          sync.RWMutex              // thread-safe
	pushCh         chan struct{}             // writer requires mutex
	loggerFactory  logging.LoggerFactory     // read-only
	log            logging.LeveledLogger     // read-only
}

// NewRouter ...
func NewRouter(config *RouterConfig) (*Router, error) {
	loggerFactory := config.LoggerFactory
	log := loggerFactory.NewLogger("vnet")

	_, ipv4Net, err := net.ParseCIDR(config.CIDR)
	if err != nil {
		return nil, err
	}

	queueSize := defaultRouterQueueSize
	if config.QueueSize > 0 {
		queueSize = config.QueueSize
	}

	// set up network interface, lo0
	lo0 := transport.NewInterface(net.Interface{
		Index:        1,
		MTU:          16384,
		Name:         lo0String,
		HardwareAddr: nil,
		Flags:        net.FlagUp | net.FlagLoopback | net.FlagMulticast,
	})
	lo0.AddAddress(&net.IPAddr{IP: net.ParseIP("127.0.0.1"), Zone: ""})

	// set up network interface, eth0
	eth0 := transport.NewInterface(net.Interface{
		Index:        2,
		MTU:          1500,
		Name:         "eth0",
		HardwareAddr: newMACAddress(),
		Flags:        net.FlagUp | net.FlagMulticast,
	})

	// local host name resolver
	resolver := newResolver(&resolverConfig{
		LoggerFactory: config.LoggerFactory,
	})

	name := config.Name
	if len(name) == 0 {
		name = assignRouterName()
	}

	var staticIPs []net.IP
	staticLocalIPs := map[string]net.IP{}
	for _, ipStr := range config.StaticIPs {
		ipPair := strings.Split(ipStr, "/")
		if ip := net.ParseIP(ipPair[0]); ip != nil {
			if len(ipPair) > 1 {
				locIP := net.ParseIP(ipPair[1])
				if locIP == nil {
					return nil, errInvalidLocalIPinStaticIPs
				}
				if !ipv4Net.Contains(locIP) {
					return nil, fmt.Errorf("local IP %s %w", locIP.String(), errLocalIPBeyondStaticIPsSubset)
				}
				staticLocalIPs[ip.String()] = locIP
			}
			staticIPs = append(staticIPs, ip)
		}
	}
	if len(config.StaticIP) > 0 {
		log.Warn("StaticIP is deprecated. Use StaticIPs instead")
		if ip := net.ParseIP(config.StaticIP); ip != nil {
			staticIPs = append(staticIPs, ip)
		}
	}

	if nStaticLocal := len(staticLocalIPs); nStaticLocal > 0 {
		if nStaticLocal != len(staticIPs) {
			return nil, errLocalIPNoStaticsIPsAssociated
		}
	}

	return &Router{
		name:           name,
		interfaces:     []*transport.Interface{lo0, eth0},
		ipv4Net:        ipv4Net,
		staticIPs:      staticIPs,
		staticLocalIPs: staticLocalIPs,
		queue:          newChunkQueue(queueSize, 0),
		natType:        config.NATType,
		nics:           map[string]NIC{},
		resolver:       resolver,
		minDelay:       config.MinDelay,
		maxJitter:      config.MaxJitter,
		pushCh:         make(chan struct{}, 1),
		loggerFactory:  loggerFactory,
		log:            log,
	}, nil
}

// caller must hold the mutex
func (r *Router) getInterfaces() ([]*transport.Interface, error) {
	if len(r.interfaces) == 0 {
		return nil, fmt.Errorf("%w is available", errNoInterface)
	}

	return r.interfaces, nil
}

func (r *Router) getInterface(ifName string) (*transport.Interface, error) {
	r.mutex.RLock()
	defer r.mutex.RUnlock()

	ifs, err := r.getInterfaces()
	if err != nil {
		return nil, err
	}
	for _, ifc := range ifs {
		if ifc.Name == ifName {
			return ifc, nil
		}
	}

	return nil, fmt.Errorf("%w: %s", transport.ErrInterfaceNotFound, ifName)
}

// Start ...
func (r *Router) Start() error {
	r.mutex.Lock()
	defer r.mutex.Unlock()

	if r.stopFunc != nil {
		return errRouterAlreadyStarted
	}

	cancelCh := make(chan struct{})

	go func() {
	loop:
		for {
			d, err := r.processChunks()
			if err != nil {
				r.log.Errorf("[%s] %s", r.name, err.Error())
				break
			}

			if d <= 0 {
				select {
				case <-r.pushCh:
				case <-cancelCh:
					break loop
				}
			} else {
				t := time.NewTimer(d)
				select {
				case <-t.C:
				case <-cancelCh:
					break loop
				}
			}
		}
	}()

	r.stopFunc = func() {
		close(cancelCh)
	}

	for _, child := range r.children {
		if err := child.Start(); err != nil {
			return err
		}
	}

	return nil
}

// Stop ...
func (r *Router) Stop() error {
	r.mutex.Lock()
	defer r.mutex.Unlock()

	if r.stopFunc == nil {
		return errRouterAlreadyStopped
	}

	for _, router := range r.children {
		r.mutex.Unlock()
		err := router.Stop()
		r.mutex.Lock()

		if err != nil {
			return err
		}
	}

	r.stopFunc()
	r.stopFunc = nil
	return nil
}

// caller must hold the mutex
func (r *Router) addNIC(nic NIC) error {
	ifc, err := nic.getInterface("eth0")
	if err != nil {
		return err
	}

	var ips []net.IP

	if ips = nic.getStaticIPs(); len(ips) == 0 {
		// assign an IP address
		ip, err2 := r.assignIPAddress()
		if err2 != nil {
			return err2
		}
		ips = append(ips, ip)
	}

	for _, ip := range ips {
		if !r.ipv4Net.Contains(ip) {
			return fmt.Errorf("%w: %s", errStaticIPisBeyondSubnet, r.ipv4Net.String())
		}

		ifc.AddAddress(&net.IPNet{
			IP:   ip,
			Mask: r.ipv4Net.Mask,
		})

		r.nics[ip.String()] = nic
	}

	return nic.setRouter(r)
}

// AddRouter adds a child Router.
func (r *Router) AddRouter(router *Router) error {
	r.mutex.Lock()
	defer r.mutex.Unlock()

	// Router is a NIC. Add it as a NIC so that packets are routed to this child
	// router.
	err := r.addNIC(router)
	if err != nil {
		return err
	}

	if err = router.setRouter(r); err != nil {
		return err
	}

	r.children = append(r.children, router)
	return nil
}

// AddChildRouter is like AddRouter, but does not add the child routers NIC to
// the parent. This has to be done manually by calling AddNet, which allows to
// use a wrapper around the subrouters NIC.
// AddNet MUST be called before AddChildRouter.
func (r *Router) AddChildRouter(router *Router) error {
	r.mutex.Lock()
	defer r.mutex.Unlock()
	if err := router.setRouter(r); err != nil {
		return err
	}

	r.children = append(r.children, router)
	return nil
}

// AddNet ...
func (r *Router) AddNet(nic NIC) error {
	r.mutex.Lock()
	defer r.mutex.Unlock()

	return r.addNIC(nic)
}

// AddHost adds a mapping of hostname and an IP address to the local resolver.
func (r *Router) AddHost(hostName string, ipAddr string) error {
	return r.resolver.addHost(hostName, ipAddr)
}

// AddChunkFilter adds a filter for chunks traversing this router.
// You may add more than one filter. The filters are called in the order of this method call.
// If a chunk is dropped by a filter, subsequent filter will not receive the chunk.
func (r *Router) AddChunkFilter(filter ChunkFilter) {
	r.mutex.Lock()
	defer r.mutex.Unlock()

	r.chunkFilters = append(r.chunkFilters, filter)
}

// caller should hold the mutex
func (r *Router) assignIPAddress() (net.IP, error) {
	// See: https://stackoverflow.com/questions/14915188/ip-address-ending-with-zero

	if r.lastID == 0xfe {
		return nil, errAddressSpaceExhausted
	}

	ip := make(net.IP, 4)
	copy(ip, r.ipv4Net.IP[:3])
	r.lastID++
	ip[3] = r.lastID
	return ip, nil
}

func (r *Router) push(c Chunk) {
	r.mutex.Lock()
	defer r.mutex.Unlock()

	r.log.Debugf("[%s] route %s", r.name, c.String())
	if r.stopFunc != nil {
		c.setTimestamp()
		if r.queue.push(c) {
			select {
			case r.pushCh <- struct{}{}:
			default:
			}
		} else {
			r.log.Warnf("[%s] queue was full. dropped a chunk", r.name)
		}
	}
}

func (r *Router) processChunks() (time.Duration, error) {
	r.mutex.Lock()
	defer r.mutex.Unlock()

	// Introduce jitter by delaying the processing of chunks.
	if r.maxJitter > 0 {
		jitter := time.Duration(rand.Int63n(int64(r.maxJitter))) //nolint:gosec
		time.Sleep(jitter)
	}

	//      cutOff
	//         v min delay
	//         |<--->|
	//  +------------:--
	//  |OOOOOOXXXXX :   --> time
	//  +------------:--
	//  |<--->|     now
	//    due

	enteredAt := time.Now()
	cutOff := enteredAt.Add(-r.minDelay)

	var d time.Duration // the next sleep duration

	for {
		d = 0

		c := r.queue.peek()
		if c == nil {
			break // no more chunk in the queue
		}

		// check timestamp to find if the chunk is due
		if c.getTimestamp().After(cutOff) {
			// There is one or more chunk in the queue but none of them are due.
			// Calculate the next sleep duration here.
			nextExpire := c.getTimestamp().Add(r.minDelay)
			d = nextExpire.Sub(enteredAt)
			break
		}

		var ok bool
		if c, ok = r.queue.pop(); !ok {
			break // no more chunk in the queue
		}

		blocked := false
		for i := 0; i < len(r.chunkFilters); i++ {
			filter := r.chunkFilters[i]
			if !filter(c) {
				blocked = true
				break
			}
		}
		if blocked {
			continue // discard
		}

		dstIP := c.getDestinationIP()

		// check if the destination is in our subnet
		if r.ipv4Net.Contains(dstIP) {
			// search for the destination NIC
			var nic NIC
			if nic, ok = r.nics[dstIP.String()]; !ok {
				// NIC not found. drop it.
				r.log.Debugf("[%s] %s unreachable", r.name, c.String())
				continue
			}

			// found the NIC, forward the chunk to the NIC.
			// call to NIC must unlock mutex
			r.mutex.Unlock()
			nic.onInboundChunk(c)
			r.mutex.Lock()
			continue
		}

		// the destination is outside of this subnet
		// is this WAN?
		if r.parent == nil {
			// this WAN. No route for this chunk
			r.log.Debugf("[%s] no route found for %s", r.name, c.String())
			continue
		}

		// Pass it to the parent via NAT
		toParent, err := r.nat.translateOutbound(c)
		if err != nil {
			return 0, err
		}

		if toParent == nil {
			continue
		}

		//nolint:godox
		/* FIXME: this implementation would introduce a duplicate packet!
		if r.nat.natType.Hairpinning {
			hairpinned, err := r.nat.translateInbound(toParent)
			if err != nil {
				r.log.Warnf("[%s] %s", r.name, err.Error())
			} else {
				go func() {
					r.push(hairpinned)
				}()
			}
		}
		*/

		// call to parent router mutex unlock mutex
		r.mutex.Unlock()
		r.parent.push(toParent)
		r.mutex.Lock()
	}

	return d, nil
}

// caller must hold the mutex
func (r *Router) setRouter(parent *Router) error {
	r.parent = parent
	r.resolver.setParent(parent.resolver)

	// when this method is called, one or more IP address has already been assigned by
	// the parent router.
	ifc, err := r.getInterface("eth0")
	if err != nil {
		return err
	}

	addrs, _ := ifc.Addrs()
	if len(addrs) == 0 {
		return errNoIPAddrEth0
	}

	mappedIPs := []net.IP{}
	localIPs := []net.IP{}

	for _, ifcAddr := range addrs {
		var ip net.IP
		switch addr := ifcAddr.(type) {
		case *net.IPNet:
			ip = addr.IP
		case *net.IPAddr: // Do we really need this case?
			ip = addr.IP
		default:
		}

		if ip == nil {
			continue
		}

		mappedIPs = append(mappedIPs, ip)

		if locIP := r.staticLocalIPs[ip.String()]; locIP != nil {
			localIPs = append(localIPs, locIP)
		}
	}

	// Set up NAT here
	if r.natType == nil {
		r.natType = &NATType{
			MappingBehavior:   EndpointIndependent,
			FilteringBehavior: EndpointAddrPortDependent,
			Hairpinning:       false,
			PortPreservation:  false,
			MappingLifeTime:   30 * time.Second,
		}
	}
	r.nat, err = newNAT(&natConfig{
		name:          r.name,
		natType:       *r.natType,
		mappedIPs:     mappedIPs,
		localIPs:      localIPs,
		loggerFactory: r.loggerFactory,
	})
	if err != nil {
		return err
	}

	return nil
}

func (r *Router) onInboundChunk(c Chunk) {
	fromParent, err := r.nat.translateInbound(c)
	if err != nil {
		r.log.Warnf("[%s] %s", r.name, err.Error())
		return
	}

	r.push(fromParent)
}

func (r *Router) getStaticIPs() []net.IP {
	return r.staticIPs
}
