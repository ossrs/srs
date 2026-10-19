package main

import (
	"fmt"
	"hash/fnv"
)

// sourceTimeline gives a player's frames their publisher time. SRS's default
// time_jitter full starts each player's stream near zero: a jump of more than
// 250ms, such as from the cached sequence headers to the first frame of the GOP
// cache, is stamped as 10ms. So a player that joins after the first IDR gets
// timestamps about one GOP smaller than the publisher's, though it is live.
// The publisher sends the same source in a loop, so a video frame's payload
// names its time in the loop, and the loop nearest to the publisher's newest
// time gives its publisher time.
type sourceTimeline struct {
	// duration is one loop, as publishRTMP sends it.
	duration uint32
	// at maps a video frame's payload hash to its time in the loop, or -1 when
	// more than one frame has that payload.
	at map[uint64]int64
	// frames counts the video frames with a unique payload.
	frames int
}

func loopDuration(tags []rtmpMessage) uint32 { return tags[len(tags)-1].timestamp + 40 }

func payloadHash(payload []byte) uint64 {
	h := fnv.New64a()
	h.Write(payload)
	return h.Sum64()
}

// isVideoFrame is a video message other than a sequence header, which SRS
// sends from its cache, out of the stream's time order.
func isVideoFrame(m rtmpMessage) bool {
	if m.typeID != 9 || len(m.payload) < 2 {
		return false
	}
	if m.payload[0]&0x80 != 0 {
		// Enhanced RTMP: the packet type is in the low bits; 0 is SequenceStart.
		return m.payload[0]&0x0f != 0
	}
	return m.payload[1] != 0
}

func newSourceTimeline(tags []rtmpMessage) *sourceTimeline {
	if len(tags) == 0 {
		return nil
	}
	s := &sourceTimeline{duration: loopDuration(tags), at: make(map[uint64]int64)}
	for _, tag := range tags {
		if !isVideoFrame(tag) {
			continue
		}
		h := payloadHash(tag.payload)
		if _, seen := s.at[h]; seen {
			s.at[h] = -1
			continue
		}
		s.at[h] = int64(tag.timestamp)
	}
	for _, at := range s.at {
		if at >= 0 {
			s.frames++
		}
	}
	return s
}

// lagTimeline is the source's timeline, or an error when no video frame has a
// unique payload, since then no player's lag could be measured.
func lagTimeline(media string, tags []rtmpMessage) (*sourceTimeline, error) {
	s := newSourceTimeline(tags)
	if s == nil || s.frames == 0 {
		return nil, fmt.Errorf("source %s has no video frame with a unique payload to measure lag by", media)
	}
	return s, nil
}

// inLoop is the time in the loop of a video frame with a unique payload.
func (s *sourceTimeline) inLoop(m rtmpMessage) (uint32, bool) {
	if s == nil || !isVideoFrame(m) {
		return 0, false
	}
	at, ok := s.at[payloadHash(m.payload)]
	if !ok || at < 0 {
		return 0, false
	}
	return uint32(at), true
}

// publisherTime is the publisher time of a frame at inLoop: of its loops, the
// one nearest to the publisher's newest time, so a frame up to half a loop
// behind or ahead is placed right.
func (s *sourceTimeline) publisherTime(inLoop, newest uint32) int64 {
	d := int64(s.duration)
	n := (int64(newest) - int64(inLoop) + d/2) / d
	if n < 0 {
		n = 0
	}
	return n*d + int64(inLoop)
}
