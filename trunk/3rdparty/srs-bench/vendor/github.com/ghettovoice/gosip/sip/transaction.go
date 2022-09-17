package sip

type TransactionKey string

func (key TransactionKey) String() string {
	return string(key)
}

type Transaction interface {
	Origin() Request
	Key() TransactionKey
	String() string
	Errors() <-chan error
	Done() <-chan bool
}

type ServerTransaction interface {
	Transaction
	Respond(res Response) error
	Acks() <-chan Request
	Cancels() <-chan Request
}

type ClientTransaction interface {
	Transaction
	Responses() <-chan Response
	Cancel() error
}
