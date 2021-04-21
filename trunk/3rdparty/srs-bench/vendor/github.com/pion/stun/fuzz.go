// +build gofuzz

package stun

import (
	"encoding/binary"
	"fmt"
)

var (
	m = New()
)

// FuzzMessage is go-fuzz endpoint for message.
func FuzzMessage(data []byte) int {
	m.Reset()
	// fuzzer dont know about cookies
	binary.BigEndian.PutUint32(data[4:8], magicCookie)
	// trying to read data as message
	if _, err := m.Write(data); err != nil {
		return 0
	}
	m2 := New()
	if _, err := m2.Write(m.Raw); err != nil {
		panic(err) // nolint
	}
	if m2.TransactionID != m.TransactionID {
		panic("transaction ID mismatch") // nolint
	}
	if m2.Type != m.Type {
		panic("type missmatch") // nolint
	}
	if len(m2.Attributes) != len(m.Attributes) {
		panic("attributes length missmatch") // nolint
	}
	return 1
}

// FuzzType is go-fuzz endpoint for message type.
func FuzzType(data []byte) int {
	t := MessageType{}
	vt, _ := binary.Uvarint(data)
	v := uint16(vt) & 0x1fff // first 3 bits are empty
	t.ReadValue(v)
	v2 := t.Value()
	if v != v2 {
		panic("v != v2") // nolint
	}
	t2 := MessageType{}
	t2.ReadValue(v2)
	if t2 != t {
		panic("t2 != t") // nolint
	}
	return 0
}

type attr interface {
	Getter
	Setter
}

type attrs []struct {
	g attr
	t AttrType
}

func (a attrs) pick(v byte) struct {
	g attr
	t AttrType
} {
	idx := int(v) % len(a)
	return a[idx]
}

func FuzzSetters(data []byte) int {
	var (
		m1 = &Message{
			Raw: make([]byte, 0, 2048),
		}
		m2 = &Message{
			Raw: make([]byte, 0, 2048),
		}
		m3 = &Message{
			Raw: make([]byte, 0, 2048),
		}
	)
	attributes := attrs{
		{new(Realm), AttrRealm},
		{new(XORMappedAddress), AttrXORMappedAddress},
		{new(Nonce), AttrNonce},
		{new(Software), AttrSoftware},
		{new(AlternateServer), AttrAlternateServer},
		{new(ErrorCodeAttribute), AttrErrorCode},
		{new(UnknownAttributes), AttrUnknownAttributes},
		{new(Username), AttrUsername},
		{new(MappedAddress), AttrMappedAddress},
		{new(Realm), AttrRealm},
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
	if err == ErrAttributeNotFound {
		fmt.Println("unexpected 404") // nolint
		panic(err)                    // nolint
	}
	if err != nil {
		return 1
	}
	m2.WriteHeader()
	if err = a.g.AddTo(m2); err != nil {
		// We allow decoding some text attributes
		// when their length is too big, but
		// not encoding.
		if !IsAttrSizeOverflow(err) {
			panic(err) // nolint
		}
		return 1
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
