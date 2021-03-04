package webrtc

// DataChannelParameters describes the configuration of the DataChannel.
type DataChannelParameters struct {
	Label             string  `json:"label"`
	Protocol          string  `json:"protocol"`
	ID                *uint16 `json:"id"`
	Ordered           bool    `json:"ordered"`
	MaxPacketLifeTime *uint16 `json:"maxPacketLifeTime"`
	MaxRetransmits    *uint16 `json:"maxRetransmits"`
	Negotiated        bool    `json:"negotiated"`
}
