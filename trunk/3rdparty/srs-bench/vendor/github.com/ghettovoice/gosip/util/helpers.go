// Forked from github.com/StefanKopieczek/gossip by @StefanKopieczek
package util

import (
	"errors"
	"net"
	"sync"
)

// Check two string pointers for equality as follows:
// - If neither pointer is nil, check equality of the underlying strings.
// - If either pointer is nil, return true if and only if they both are.
func StrPtrEq(a *string, b *string) bool {
	if a == nil || b == nil {
		return a == b
	}

	return *a == *b
}

// Check two uint16 pointers for equality as follows:
// - If neither pointer is nil, check equality of the underlying uint16s.
// - If either pointer is nil, return true if and only if they both are.
func Uint16PtrEq(a *uint16, b *uint16) bool {
	if a == nil || b == nil {
		return a == b
	}

	return *a == *b
}

func Coalesce(arg1 interface{}, arg2 interface{}, args ...interface{}) interface{} {
	all := append([]interface{}{arg1, arg2}, args...)
	for _, arg := range all {
		if arg != nil {
			return arg
		}
	}

	return nil
}

func Noop() {}

func MergeErrs(chs ...<-chan error) <-chan error {
	wg := new(sync.WaitGroup)
	out := make(chan error)

	pipe := func(ch <-chan error) {
		defer wg.Done()
		for err := range ch {
			out <- err
		}
	}

	wg.Add(len(chs))
	for _, ch := range chs {
		go pipe(ch)
	}

	go func() {
		wg.Wait()
		close(out)
	}()

	return out
}

func ResolveSelfIP() (net.IP, error) {
	ifaces, err := net.Interfaces()
	if err != nil {
		return nil, err
	}
	for _, iface := range ifaces {
		if iface.Flags&net.FlagUp == 0 {
			continue // interface down
		}
		if iface.Flags&net.FlagLoopback != 0 {
			continue // loopback interface
		}
		addrs, err := iface.Addrs()
		if err != nil {
			return nil, err
		}
		for _, addr := range addrs {
			var ip net.IP
			switch v := addr.(type) {
			case *net.IPNet:
				ip = v.IP
			case *net.IPAddr:
				ip = v.IP
			}
			if ip == nil || ip.IsLoopback() {
				continue
			}
			ip = ip.To4()
			if ip == nil {
				continue // not an ipv4 address
			}
			return ip, nil
		}
	}
	return nil, errors.New("server not connected to any network")
}
