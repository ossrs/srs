package datachannel

// channelAck is used to ACK a DataChannel open
type channelAck struct{}

const (
	channelOpenAckLength = 4
)

// Marshal returns raw bytes for the given message
func (c *channelAck) Marshal() ([]byte, error) {
	raw := make([]byte, channelOpenAckLength)
	raw[0] = uint8(dataChannelAck)

	return raw, nil
}

// Unmarshal populates the struct with the given raw data
func (c *channelAck) Unmarshal(raw []byte) error {
	// Message type already checked in Parse and there is no further data
	return nil
}
