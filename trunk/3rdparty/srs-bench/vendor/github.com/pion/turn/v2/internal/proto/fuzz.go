// +build gofuzz

package proto

import (
	"fmt"

	"github.com/pion/stun"
)

type attr interface {
	stun.Getter
	stun.Setter
}

type attrs []struct {
	g attr
	t stun.AttrType
}

func (a attrs) pick(v byte) struct {
	g attr
	t stun.AttrType
} {
	idx := int(v) % len(a)
	return a[idx]
}

func FuzzSetters(data []byte) int {
	var (
		m1 = &stun.Message{
			Raw: make([]byte, 0, 2048),
		}
		m2 = &stun.Message{
			Raw: make([]byte, 0, 2048),
		}
		m3 = &stun.Message{
			Raw: make([]byte, 0, 2048),
		}
	)
	attributes := attrs{
		{new(RequestedTransport), stun.AttrRequestedTransport},
		{new(RelayedAddress), stun.AttrXORRelayedAddress},
		{new(ChannelNumber), stun.AttrChannelNumber},
		{new(Data), stun.AttrData},
		{new(EvenPort), stun.AttrEvenPort},
		{new(Lifetime), stun.AttrLifetime},
		{new(ReservationToken), stun.AttrReservationToken},
	}
	var firstByte = byte(0)
	if len(data) > 0 {
		firstByte = data[0]
	}
	a := attributes.pick(firstByte)
	value := data
	if len(data) > 1 {
		value = value[1:]
	}
	m1.WriteHeader()
	m1.Add(a.t, value)
	err := a.g.GetFrom(m1)
	if err == stun.ErrAttributeNotFound {
		fmt.Println("unexpected 404") // nolint
		panic(err)                    // nolint
	}
	if err != nil {
		return 1
	}
	m2.WriteHeader()
	if err := a.g.AddTo(m2); err != nil {
		fmt.Println("failed to add attribute to m2") // nolint
		panic(err)                                   // nolint
	}
	m3.WriteHeader()
	v, err := m2.Get(a.t)
	if err != nil {
		panic(err) // nolint
	}
	m3.Add(a.t, v)

	if !m2.Equal(m3) {
		fmt.Println(m2, "not equal", m3) // nolint
		panic("not equal")               // nolint
	}
	return 1
}

var d = &ChannelData{}

func FuzzChannelData(data []byte) int {
	d.Reset()
	if b := bin.Uint16(data[0:4]); b > 20000 {
		bin.PutUint16(data[0:4], MinChannelNumber-1)
	} else if b > 40000 {
		bin.PutUint16(data[0:4], MinChannelNumber+(MaxChannelNumber-MinChannelNumber)%b)
	}
	d.Raw = append(d.Raw, data...)
	if d.Decode() != nil {
		return 0
	}
	d2 := &ChannelData{}
	d.Encode()
	if !d.Number.Valid() {
		return 1
	}
	d2.Raw = d.Raw
	if err := d2.Decode(); err != nil {
		panic(err) //nolint
	}
	return 1
}
