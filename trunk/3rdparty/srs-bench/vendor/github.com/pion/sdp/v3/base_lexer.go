// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sdp

import (
	"errors"
	"fmt"
	"io"
	"strconv"
)

var errDocumentStart = errors.New("already on document start")

type syntaxError struct {
	s string
	i int
}

func (e syntaxError) Error() string {
	if e.i < 0 {
		e.i = 0
	}

	return fmt.Sprintf("sdp: syntax error at pos %d: %s", e.i, strconv.QuoteToASCII(e.s[e.i:e.i+1]))
}

type baseLexer struct {
	value string
	pos   int
}

func (l baseLexer) syntaxError() error {
	return syntaxError{s: l.value, i: l.pos - 1}
}

func (l *baseLexer) unreadByte() error {
	if l.pos <= 0 {
		return errDocumentStart
	}
	l.pos--

	return nil
}

func (l *baseLexer) readByte() (byte, error) {
	if l.pos >= len(l.value) {
		return byte(0), io.EOF
	}
	ch := l.value[l.pos]
	l.pos++

	return ch, nil
}

func (l *baseLexer) nextLine() error {
	for {
		ch, err := l.readByte()
		if errors.Is(err, io.EOF) {
			return nil
		} else if err != nil {
			return err
		}
		if !isNewline(ch) {
			return l.unreadByte()
		}
	}
}

func (l *baseLexer) readWhitespace() error { //notlint:cyclop
	for {
		ch, err := l.readByte()
		if errors.Is(err, io.EOF) {
			return nil
		} else if err != nil {
			return err
		}
		if !isWhitespace(ch) {
			return l.unreadByte()
		}
	}
}

func (l *baseLexer) readUint64Field() (i uint64, err error) { //nolint:cyclop
	for {
		ch, err := l.readByte()
		if errors.Is(err, io.EOF) && i > 0 {
			break
		} else if err != nil {
			return i, err
		}

		if isNewline(ch) {
			if err := l.unreadByte(); err != nil {
				return i, err
			}

			break
		}

		if isWhitespace(ch) {
			if err := l.readWhitespace(); err != nil {
				return i, err
			}

			break
		}

		switch ch {
		case '0':
			i *= 10
		case '1':
			i = i*10 + 1
		case '2':
			i = i*10 + 2
		case '3':
			i = i*10 + 3
		case '4':
			i = i*10 + 4
		case '5':
			i = i*10 + 5
		case '6':
			i = i*10 + 6
		case '7':
			i = i*10 + 7
		case '8':
			i = i*10 + 8
		case '9':
			i = i*10 + 9
		default:
			return i, l.syntaxError()
		}
	}

	return i, nil
}

// Returns next field on this line or empty string if no more fields on line.
func (l *baseLexer) readField() (string, error) {
	start := l.pos
	var stop int
	for {
		stop = l.pos
		ch, err := l.readByte()
		if errors.Is(err, io.EOF) && stop > start {
			break
		} else if err != nil {
			return "", err
		}

		if isNewline(ch) {
			if err := l.unreadByte(); err != nil {
				return "", err
			}

			break
		}

		if isWhitespace(ch) {
			if err := l.readWhitespace(); err != nil {
				return "", err
			}

			break
		}
	}

	return l.value[start:stop], nil
}

// Returns symbols until line end.
func (l *baseLexer) readLine() (string, error) {
	start := l.pos
	trim := 1
	for {
		ch, err := l.readByte()
		if err != nil {
			return "", err
		}
		if ch == '\r' {
			trim++
		}
		if ch == '\n' {
			return l.value[start : l.pos-trim], nil
		}
	}
}

func (l *baseLexer) readType() (byte, error) {
	for {
		firstByte, err := l.readByte()
		if err != nil {
			return 0, err
		}

		if isNewline(firstByte) {
			continue
		}

		secondByte, err := l.readByte()
		if err != nil {
			return 0, err
		}

		if secondByte != '=' {
			return firstByte, l.syntaxError()
		}

		return firstByte, nil
	}
}

func isNewline(ch byte) bool { return ch == '\n' || ch == '\r' }

func isWhitespace(ch byte) bool { return ch == ' ' || ch == '\t' }

func anyOf(element string, data ...string) bool {
	for _, v := range data {
		if element == v {
			return true
		}
	}

	return false
}
