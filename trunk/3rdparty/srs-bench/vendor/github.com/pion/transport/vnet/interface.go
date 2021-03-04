package vnet

import (
	"errors"
	"net"
)

var errNoAddressAssigned = errors.New("no address assigned")

// See: https://play.golang.org/p/nBO9KGYEziv

// InterfaceBase ...
type InterfaceBase net.Interface

// Interface ...
type Interface struct {
	InterfaceBase
	addrs []net.Addr
}

// NewInterface ...
func NewInterface(ifc net.Interface) *Interface {
	return &Interface{
		InterfaceBase: InterfaceBase(ifc),
		addrs:         nil,
	}
}

// AddAddr ...
func (ifc *Interface) AddAddr(addr net.Addr) {
	ifc.addrs = append(ifc.addrs, addr)
}

// Addrs ...
func (ifc *Interface) Addrs() ([]net.Addr, error) {
	if len(ifc.addrs) == 0 {
		return nil, errNoAddressAssigned
	}
	return ifc.addrs, nil
}
