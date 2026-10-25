package main

import (
	"errors"
	"fmt"
	"net"
	"os"
	"sync"
	"sync/atomic"
	"time"
)

// clientRuntime is a worker's share of a step's clients: players of the
// parent's publisher, publishers, or streams, sampled once a second.
type clientRuntime interface {
	sample(timestamps []uint32, final bool) workerResponse
	close()
}

// streamName is the stream of a step's client i, so each publisher of a
// publisher or many-streams step has its own.
func streamName(prefix string, i int) string { return fmt.Sprintf("%s-%d", prefix, i) }

// clientOffsets is the index of each worker's first client in the step.
func clientOffsets(shares []int) []int {
	offsets := make([]int, len(shares))
	for i := 1; i < len(shares); i++ {
		offsets[i] = offsets[i-1] + shares[i-1]
	}
	return offsets
}

// publishFunc publishes the source to one stream until stop.
type publishFunc func(stream string, clock *mediaClock, stop <-chan struct{}) error

func publisherOf(kind, media string, tags []rtmpMessage) (publishFunc, error) {
	publish, ok := publishers[kind]
	if !ok {
		return nil, fmt.Errorf("no publisher for protocol %q", kind)
	}
	return func(stream string, clock *mediaClock, stop <-chan struct{}) error {
		return publish(stream, media, tags, clock, stop)
	}, nil
}

// startClients starts a worker's share of n clients of the scenario, client i
// of the step being first+i: players split over the streams, or publishers and
// streams named by the step's prefix, the one stream given.
func startClients(s scenario, n int, streams []string, first int, media string, tags []rtmpMessage, timeline *sourceTimeline) (clientRuntime, error) {
	names := func(i int) string { return streamName(streams[0], first+i) }
	switch s.Unit {
	case "publishers":
		publish, err := publisherOf(s.Publisher, media, tags)
		if err != nil {
			return nil, err
		}
		return startPublisherClients(publish, n, names, s.gap()), nil
	case "streams":
		publish, err := publisherOf(s.Publisher, media, tags)
		if err != nil {
			return nil, err
		}
		dial, err := playerDialer(s.Player)
		if err != nil {
			return nil, err
		}
		return startStreams(publish, dial, n, names, timeline, s.gap()), nil
	}
	dial, err := playerDialer(s.Player)
	if err != nil {
		return nil, err
	}
	index := playerStreamIndex(first, n, len(streams))
	players := startPlayersOn(dial, n, timeline, s.gap(), func(id int) string { return streams[index[id]] }, nil)
	players.streams = index
	return players, nil
}

// workerPublishers are N publishers, each of its own stream, measured by what
// they send: the tags and bytes, and how far each is behind its real-time
// pace, its lag. A TCP publisher falls behind when SRS stops reading; a UDP one
// never blocks, so it fails only when its session fails.
type workerPublishers struct {
	stop     chan struct{}
	started  time.Time
	gap      time.Duration
	wg       sync.WaitGroup
	clocks   []*mediaClock
	ended    []atomic.Bool
	failures atomic.Int64
}

// startPublisherClients starts n publishers, publisher i on stream streamOf(i).
// A publisher whose connect times out before it sent anything, as in a
// connect burst, tries again until stop; one that ends otherwise before stop
// fails once and is not judged again.
func startPublisherClients(publish publishFunc, n int, streamOf func(int) string, gap time.Duration) *workerPublishers {
	w := &workerPublishers{stop: make(chan struct{}), started: time.Now(), gap: gap, clocks: make([]*mediaClock, n), ended: make([]atomic.Bool, n)}
	var logged atomic.Bool
	for i := range w.clocks {
		w.clocks[i] = &mediaClock{}
		w.wg.Add(1)
		go func(i int) {
			defer w.wg.Done()
			for {
				err := publish(streamOf(i), w.clocks[i], w.stop)
				select {
				case <-w.stop:
					return
				default:
				}
				var netErr net.Error
				if errors.As(err, &netErr) && netErr.Timeout() && !w.clocks[i].published() {
					continue
				}
				if err == nil {
					err = errors.New("publisher stopped")
				}
				if logged.CompareAndSwap(false, true) {
					fmt.Fprintf(os.Stderr, "publisher %s: %v\n", streamOf(i), err)
				}
				w.ended[i].Store(true)
				w.failures.Add(1)
				return
			}
		}(i)
	}
	return w
}

// sample counts the tags and bytes sent in the last second; a publisher fails
// when it ended, or sent nothing for the gap, or nothing in the whole step at
// its last sample. Its lag is the most any publisher is behind its pace.
func (w *workerPublishers) sample(_ []uint32, final bool) workerResponse {
	r := workerResponse{Failures: w.failures.Swap(0)}
	now := time.Now()
	for i, c := range w.clocks {
		frames, bytes, last := c.take()
		r.Frames += frames
		r.Bytes += bytes
		if w.ended[i].Load() {
			continue
		}
		if last.IsZero() {
			if final || now.Sub(w.started) > w.gap {
				r.Failures++
			}
			continue
		}
		if now.Sub(last) > w.gap {
			r.Failures++
		}
		if lag, ok := c.behind(now); ok && lag > r.LagMillis {
			r.LagMillis = lag
		}
	}
	return r
}

func (w *workerPublishers) close() { close(w.stop); w.wg.Wait() }

// workerStreams are N streams, each with one publisher and two players of its
// stream; players 2i and 2i+1 play stream i once its publisher sends, and
// their lag is against that publisher. Frames and bytes are the players'.
type workerStreams struct {
	publishers *workerPublishers
	players    *workerPlayers
}

func startStreams(publish publishFunc, dial func() (player, error), n int, streamOf func(int) string, timeline *sourceTimeline, gap time.Duration) *workerStreams {
	pubs := startPublisherClients(publish, n, streamOf, gap)
	clocks := make([]*mediaClock, 2*n)
	for id := range clocks {
		clocks[id] = pubs.clocks[id/2]
	}
	wait := func(id int, stop <-chan struct{}) bool {
		for !clocks[id].published() {
			select {
			case <-stop:
				return false
			case <-time.After(10 * time.Millisecond):
			}
		}
		return true
	}
	players := startPlayersOn(dial, 2*n, timeline, gap, func(id int) string { return streamOf(id / 2) }, wait)
	players.clocks = clocks
	return &workerStreams{publishers: pubs, players: players}
}

func (w *workerStreams) sample(_ []uint32, final bool) workerResponse {
	p := w.publishers.sample(nil, final)
	r := w.players.sample(nil, final)
	r.Failures += p.Failures
	if p.LagMillis > r.LagMillis {
		r.LagMillis = p.LagMillis
	}
	return r
}

func (w *workerStreams) close() { w.players.close(); w.publishers.close() }
