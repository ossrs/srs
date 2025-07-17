package headers

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/bluenviron/gortsplib/v4/pkg/base"
)

// Session is a Session header.
type Session struct {
	// session id
	Session string

	// (optional) a timeout
	Timeout *uint
}

// Unmarshal decodes a Session header.
func (h *Session) Unmarshal(v base.HeaderValue) error {
	if len(v) == 0 {
		return fmt.Errorf("value not provided")
	}

	if len(v) > 1 {
		return fmt.Errorf("value provided multiple times (%v)", v)
	}

	v0 := v[0]

	i := strings.IndexByte(v0, ';')
	if i < 0 {
		h.Session = v0
		return nil
	}

	h.Session = v0[:i]
	v0 = v0[i+1:]

	v0 = strings.TrimLeft(v0, " ")

	kvs, err := keyValParse(v0, ';')
	if err != nil {
		return err
	}

	for k, v := range kvs {
		if k == "timeout" {
			iv, err := strconv.ParseUint(v, 10, 32)
			if err != nil {
				return err
			}
			uiv := uint(iv)
			h.Timeout = &uiv
		}
	}

	return nil
}

// Marshal encodes a Session header.
func (h Session) Marshal() base.HeaderValue {
	ret := h.Session

	if h.Timeout != nil {
		ret += ";timeout=" + strconv.FormatUint(uint64(*h.Timeout), 10)
	}

	return base.HeaderValue{ret}
}
