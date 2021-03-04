package vnet

import (
	"errors"
	"fmt"
	"net"
	"sync"

	"github.com/pion/logging"
)

var (
	errHostnameEmpty       = errors.New("host name must not be empty")
	errFailedtoParseIPAddr = errors.New("failed to parse IP address")
)

type resolverConfig struct {
	LoggerFactory logging.LoggerFactory
}

type resolver struct {
	parent *resolver             // read-only
	hosts  map[string]net.IP     // requires mutex
	mutex  sync.RWMutex          // thread-safe
	log    logging.LeveledLogger // read-only
}

func newResolver(config *resolverConfig) *resolver {
	r := &resolver{
		hosts: map[string]net.IP{},
		log:   config.LoggerFactory.NewLogger("vnet"),
	}

	if err := r.addHost("localhost", "127.0.0.1"); err != nil {
		r.log.Warn("failed to add localhost to resolver")
	}
	return r
}

func (r *resolver) setParent(parent *resolver) {
	r.mutex.Lock()
	defer r.mutex.Unlock()

	r.parent = parent
}

func (r *resolver) addHost(name string, ipAddr string) error {
	r.mutex.Lock()
	defer r.mutex.Unlock()

	if len(name) == 0 {
		return errHostnameEmpty
	}
	ip := net.ParseIP(ipAddr)
	if ip == nil {
		return fmt.Errorf("%w \"%s\"", errFailedtoParseIPAddr, ipAddr)
	}
	r.hosts[name] = ip
	return nil
}

func (r *resolver) lookUp(hostName string) (net.IP, error) {
	ip := func() net.IP {
		r.mutex.RLock()
		defer r.mutex.RUnlock()

		if ip2, ok := r.hosts[hostName]; ok {
			return ip2
		}
		return nil
	}()
	if ip != nil {
		return ip, nil
	}

	// mutex must be unlocked before calling into parent resolver

	if r.parent != nil {
		return r.parent.lookUp(hostName)
	}

	return nil, &net.DNSError{
		Err:         "host not found",
		Name:        hostName,
		Server:      "vnet resolver",
		IsTimeout:   false,
		IsTemporary: false,
	}
}
