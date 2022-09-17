package sip

type Transport interface {
	Messages() <-chan Message
	Send(msg Message) error
	IsReliable(network string) bool
	IsStreamed(network string) bool
}
