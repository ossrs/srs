package rtc

import "fmt"

func FlattenErrs(errors []error) error {
	if len(errors) == 0 {
		return nil
	}
	return fmt.Errorf("%v", errors)
}
