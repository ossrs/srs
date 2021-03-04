// Package util provides auxiliary functions internally used in webrtc package
package util

import (
	"errors"
	"strings"

	"github.com/pion/randutil"
)

const (
	runesAlpha = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
)

// Use global random generator to properly seed by crypto grade random.
var globalMathRandomGenerator = randutil.NewMathRandomGenerator() // nolint:gochecknoglobals

// MathRandAlpha generates a mathmatical random alphabet sequence of the requested length.
func MathRandAlpha(n int) string {
	return globalMathRandomGenerator.GenerateString(n, runesAlpha)
}

// RandUint32 generates a mathmatical random uint32.
func RandUint32() uint32 {
	return globalMathRandomGenerator.Uint32()
}

// FlattenErrs flattens multiple errors into one
func FlattenErrs(errs []error) error {
	errs2 := []error{}
	for _, e := range errs {
		if e != nil {
			errs2 = append(errs2, e)
		}
	}
	if len(errs2) == 0 {
		return nil
	}
	return multiError(errs2)
}

type multiError []error

func (me multiError) Error() string {
	var errstrings []string

	for _, err := range me {
		if err != nil {
			errstrings = append(errstrings, err.Error())
		}
	}

	if len(errstrings) == 0 {
		return "multiError must contain multiple error but is empty"
	}

	return strings.Join(errstrings, "\n")
}

func (me multiError) Is(err error) bool {
	for _, e := range me {
		if errors.Is(e, err) {
			return true
		}
		if me2, ok := e.(multiError); ok {
			if me2.Is(err) {
				return true
			}
		}
	}
	return false
}
