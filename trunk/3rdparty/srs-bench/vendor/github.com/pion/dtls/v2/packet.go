package dtls

type packet struct {
	record                   *recordLayer
	shouldEncrypt            bool
	resetLocalSequenceNumber bool
}
