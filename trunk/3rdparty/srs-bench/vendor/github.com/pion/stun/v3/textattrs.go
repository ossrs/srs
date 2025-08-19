// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package stun

// NewUsername returns Username with provided value.
func NewUsername(username string) Username {
	return Username(username)
}

// Username represents USERNAME attribute.
//
// RFC 5389 Section 15.3
type Username []byte

func (u Username) String() string {
	return string(u)
}

const maxUsernameB = 513

// AddTo adds USERNAME attribute to message.
func (u Username) AddTo(m *Message) error {
	return TextAttribute(u).AddToAs(m, AttrUsername, maxUsernameB)
}

// GetFrom gets USERNAME from message.
func (u *Username) GetFrom(m *Message) error {
	return (*TextAttribute)(u).GetFromAs(m, AttrUsername)
}

// NewRealm returns Realm with provided value.
// Must be SASL-prepared.
func NewRealm(realm string) Realm {
	return Realm(realm)
}

// Realm represents REALM attribute.
//
// RFC 5389 Section 15.7
type Realm []byte

func (n Realm) String() string {
	return string(n)
}

const maxRealmB = 763

// AddTo adds NONCE to message.
func (n Realm) AddTo(m *Message) error {
	return TextAttribute(n).AddToAs(m, AttrRealm, maxRealmB)
}

// GetFrom gets REALM from message.
func (n *Realm) GetFrom(m *Message) error {
	return (*TextAttribute)(n).GetFromAs(m, AttrRealm)
}

const softwareRawMaxB = 763

// Software is SOFTWARE attribute.
//
// RFC 5389 Section 15.10
type Software []byte

func (s Software) String() string {
	return string(s)
}

// NewSoftware returns *Software from string.
func NewSoftware(software string) Software {
	return Software(software)
}

// AddTo adds Software attribute to m.
func (s Software) AddTo(m *Message) error {
	return TextAttribute(s).AddToAs(m, AttrSoftware, softwareRawMaxB)
}

// GetFrom decodes Software from m.
func (s *Software) GetFrom(m *Message) error {
	return (*TextAttribute)(s).GetFromAs(m, AttrSoftware)
}

// Nonce represents NONCE attribute.
//
// RFC 5389 Section 15.8
type Nonce []byte

// NewNonce returns new Nonce from string.
func NewNonce(nonce string) Nonce {
	return Nonce(nonce)
}

func (n Nonce) String() string {
	return string(n)
}

const maxNonceB = 763

// AddTo adds NONCE to message.
func (n Nonce) AddTo(m *Message) error {
	return TextAttribute(n).AddToAs(m, AttrNonce, maxNonceB)
}

// GetFrom gets NONCE from message.
func (n *Nonce) GetFrom(m *Message) error {
	return (*TextAttribute)(n).GetFromAs(m, AttrNonce)
}

// TextAttribute is helper for adding and getting text attributes.
type TextAttribute []byte

// AddToAs adds attribute with type t to m, checking maximum length. If maxLen
// is less than 0, no check is performed.
func (v TextAttribute) AddToAs(m *Message, t AttrType, maxLen int) error {
	if err := CheckOverflow(t, len(v), maxLen); err != nil {
		return err
	}
	m.Add(t, v)
	return nil
}

// GetFromAs gets t attribute from m and appends its value to reseted v.
func (v *TextAttribute) GetFromAs(m *Message, t AttrType) error {
	a, err := m.Get(t)
	if err != nil {
		return err
	}
	*v = a
	return nil
}
