package sip

import "fmt"

type RequestError struct {
	Request  Request
	Response Response
	Code     uint
	Reason   string
}

func NewRequestError(code uint, reason string, request Request, response Response) *RequestError {
	err := &RequestError{
		Code:   code,
		Reason: reason,
	}
	if request != nil {
		err.Request = CopyRequest(request)
	}
	if response != nil {
		err.Response = CopyResponse(response)
	}
	return err
}

func (err *RequestError) Error() string {
	if err == nil {
		return "<nil>"
	}

	reason := err.Reason
	if err.Code != 0 {
		reason += fmt.Sprintf(" (Code %d)", err.Code)
	}

	return fmt.Sprintf("sip.RequestError: request failed with reason '%s'", reason)
}
