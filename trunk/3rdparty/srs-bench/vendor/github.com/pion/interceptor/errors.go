package interceptor

import (
	"errors"
	"strings"
)

func flattenErrs(errs []error) error {
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
