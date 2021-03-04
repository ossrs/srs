package sctp

type chunk interface {
	unmarshal(raw []byte) error
	marshal() ([]byte, error)
	check() (bool, error)

	valueLength() int
}
