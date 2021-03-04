package srtp

type readStream interface {
	init(child streamSession, ssrc uint32) error

	Read(buf []byte) (int, error)
	GetSSRC() uint32
}
