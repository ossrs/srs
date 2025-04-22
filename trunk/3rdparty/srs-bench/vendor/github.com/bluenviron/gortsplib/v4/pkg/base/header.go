package base

import (
	"bufio"
	"fmt"
	"net/http"
	"sort"
	"strings"
)

const (
	headerMaxEntryCount  = 255
	headerMaxKeyLength   = 512
	headerMaxValueLength = 2048
)

func headerKeyNormalize(in string) string {
	switch strings.ToLower(in) {
	case "rtp-info":
		return "RTP-Info"

	case "www-authenticate":
		return "WWW-Authenticate"

	case "cseq":
		return "CSeq"
	}
	return http.CanonicalHeaderKey(in)
}

// HeaderValue is an header value.
type HeaderValue []string

// Header is a RTSP reader, present in both Requests and Responses.
type Header map[string]HeaderValue

func (h *Header) unmarshal(br *bufio.Reader) error {
	*h = make(Header)
	count := 0

	for {
		byt, err := br.ReadByte()
		if err != nil {
			return err
		}

		if byt == '\r' {
			err = readByteEqual(br, '\n')
			if err != nil {
				return err
			}
			break
		}

		if count >= headerMaxEntryCount {
			return fmt.Errorf("headers count exceeds %d", headerMaxEntryCount)
		}

		key := string([]byte{byt})
		byts, err := readBytesLimited(br, ':', headerMaxKeyLength-1)
		if err != nil {
			return fmt.Errorf("value is missing")
		}

		key += string(byts[:len(byts)-1])
		key = headerKeyNormalize(key)

		// https://tools.ietf.org/html/rfc2616
		// The field value MAY be preceded by any amount of spaces
		for {
			byt, err = br.ReadByte()
			if err != nil {
				return err
			}

			if byt != ' ' {
				break
			}
		}
		br.UnreadByte() //nolint:errcheck

		byts, err = readBytesLimited(br, '\r', headerMaxValueLength)
		if err != nil {
			return err
		}
		val := string(byts[:len(byts)-1])

		err = readByteEqual(br, '\n')
		if err != nil {
			return err
		}

		(*h)[key] = append((*h)[key], val)
		count++
	}

	return nil
}

func (h Header) marshalSize() int {
	// sort headers by key
	// in order to obtain deterministic results
	keys := make([]string, len(h))
	for key := range h {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	n := 0

	for _, key := range keys {
		for _, val := range h[key] {
			n += len([]byte(key + ": " + val + "\r\n"))
		}
	}

	n += 2

	return n
}

func (h Header) marshalTo(buf []byte) int {
	// sort headers by key
	// in order to obtain deterministic results
	keys := make([]string, len(h))
	for key := range h {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	pos := 0

	for _, key := range keys {
		for _, val := range h[key] {
			pos += copy(buf[pos:], []byte(key+": "+val+"\r\n"))
		}
	}

	pos += copy(buf[pos:], []byte("\r\n"))

	return pos
}

func (h Header) marshal() []byte {
	buf := make([]byte, h.marshalSize())
	h.marshalTo(buf)
	return buf
}
