// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"container/heap"
	"errors"
	"math/rand"
	"net"
	"sync"
	"time"
)

// Direction is a way through an Impair relay.
type Direction int

const (
	// Up is from the client to the server.
	Up Direction = iota
	// Down is from the server to the client.
	Down
)

// Shape is what an Impair relay does to the packets of one direction. The
// zero Shape passes every packet at once.
type Shape struct {
	// Loss is the percentage of packets lost, 0 to 100.
	Loss float64
	// Delay holds every packet that long.
	Delay time.Duration
	// Jitter adds to the delay of each packet a random time from -Jitter to
	// +Jitter, never below no delay, so packets also change order.
	Jitter time.Duration
	// Reorder is the percentage of packets held until the next one is sent,
	// so the two swap; a held packet with no next one goes after
	// impairHold.
	Reorder float64
	// Duplicate is the percentage of packets sent twice.
	Duplicate float64
	// Blackhole drops every packet, as a dead link, and counts them apart
	// from the loss.
	Blackhole bool
}

// ImpairCounters are the packets of one direction of an Impair relay. A
// packet that comes in is either lost, blackholed or sent, and a duplicate
// is sent once more.
type ImpairCounters struct {
	// Received is the packets that came in.
	Received int
	// Sent is the packets sent on, duplicates too.
	Sent int
	// Lost is the packets dropped by the loss.
	Lost int
	// Blackholed is the packets dropped by the blackhole.
	Blackholed int
	// Duplicated is the packets sent twice.
	Duplicated int
	// Reordered is the packets held for the next one.
	Reordered int
}

// ImpairData are the SRT data packets of one direction of an Impair relay,
// so a case can compare a peer's loss and retransmission counts with what
// the link did: the first bit of the header is 0 for a data packet, and bit
// R of its second word marks one sent again. A packet shorter than the
// header is not counted.
type ImpairData struct {
	// Received is the data packets that came in, retransmitted ones too;
	// Lost is those of them dropped by the loss.
	Received int
	Lost     int
	// Retrans is the retransmitted data packets that came in; RetransLost
	// is those of them dropped by the loss.
	Retrans     int
	RetransLost int
}

// impairSRTHeader is the size of an SRT packet header.
const impairSRTHeader = 16

// impairHold is how long a reordered packet waits for the next one.
const impairHold = 20 * time.Millisecond

// An Impair relay forwards UDP between one client and a server, both ways,
// and impairs each direction as a bad network does: loss, delay, jitter,
// reorder, duplicates and a blackhole, changed at any time with Set. The
// client dials Addr instead of the server. Rebind moves the relay's side of
// the server's connection to a new port, as a client that changes networks.
//
// Its random choices come from a seed, one source for each direction, so a
// run that sends the same packets in the same order loses the same ones;
// log Seed with a failure to replay it.
type Impair struct {
	seed   int64
	server *net.UDPAddr
	// listen is the socket the client sends to.
	listen *net.UDPConn
	pipes  [2]*impairPipe
	done   chan struct{}
	once   sync.Once
	wg     sync.WaitGroup

	mu sync.Mutex
	// client is the address the last packet from the client came from;
	// the server's packets go there.
	client *net.UDPAddr
	// upstreams are the sockets to the server, the last one the current;
	// the earlier ones still read what the server sends them.
	upstreams []*net.UDPConn
	closed    bool
}

// NewImpair starts a relay to the server at addr, a host:port, with the
// seed of its random choices. It listens on the loopback address of the
// server's family.
func NewImpair(addr string, seed int64) (*Impair, error) {
	server, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		return nil, err
	}
	local := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)}
	if server.IP.To4() == nil {
		local = &net.UDPAddr{IP: net.IPv6loopback}
	}
	listen, err := net.ListenUDP("udp", local)
	if err != nil {
		return nil, err
	}
	r := &Impair{seed: seed, server: server, listen: listen, done: make(chan struct{})}
	r.pipes[Up] = newImpairPipe(r, rand.New(rand.NewSource(seed)), r.sendUp)
	r.pipes[Down] = newImpairPipe(r, rand.New(rand.NewSource(seed+1)), r.sendDown)
	if err := r.Rebind(); err != nil {
		listen.Close()
		return nil, err
	}
	r.wg.Add(1)
	go r.readClient()
	return r, nil
}

// Addr is the address the client dials.
func (r *Impair) Addr() *net.UDPAddr {
	return r.listen.LocalAddr().(*net.UDPAddr)
}

// UpstreamAddr is the address the server sees the client from now.
func (r *Impair) UpstreamAddr() *net.UDPAddr {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.upstreams[len(r.upstreams)-1].LocalAddr().(*net.UDPAddr)
}

// Seed is the seed of the relay's random choices.
func (r *Impair) Seed() int64 {
	return r.seed
}

// Set changes what the relay does to direction d from the next packet on.
// Packets it already holds keep their time.
func (r *Impair) Set(d Direction, s Shape) {
	p := r.pipes[d]
	p.mu.Lock()
	defer p.mu.Unlock()
	p.shape = s
}

// Blackhole turns the blackhole on or off both ways, and keeps the rest of
// each direction's Shape.
func (r *Impair) Blackhole(on bool) {
	for _, p := range r.pipes {
		p.mu.Lock()
		p.shape.Blackhole = on
		p.mu.Unlock()
	}
}

// Counters are the packets of direction d so far.
func (r *Impair) Counters(d Direction) ImpairCounters {
	p := r.pipes[d]
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.counters
}

// DataCounters are the SRT data packets of direction d so far.
func (r *Impair) DataCounters(d Direction) ImpairData {
	p := r.pipes[d]
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.data
}

// Rebind opens a new socket to the server and sends the client's packets
// from it from now on, so the server sees the client on a new port. The
// earlier sockets stay open and pass on what the server still sends them.
func (r *Impair) Rebind() error {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.closed {
		return net.ErrClosed
	}
	s, err := net.DialUDP("udp", nil, r.server)
	if err != nil {
		return err
	}
	r.upstreams = append(r.upstreams, s)
	r.wg.Add(1)
	go r.readServer(s)
	return nil
}

// Close stops the relay and drops the packets it holds. It returns when no
// goroutine of the relay runs.
func (r *Impair) Close() error {
	r.once.Do(func() {
		close(r.done)
		r.mu.Lock()
		r.closed = true
		r.listen.Close()
		for _, s := range r.upstreams {
			s.Close()
		}
		r.mu.Unlock()
		for _, p := range r.pipes {
			p.stop()
		}
	})
	r.wg.Wait()
	return nil
}

func (r *Impair) readClient() {
	defer r.wg.Done()
	b := make([]byte, 65536)
	for {
		n, from, err := r.listen.ReadFromUDP(b)
		if err != nil {
			return
		}
		r.mu.Lock()
		r.client = from
		r.mu.Unlock()
		r.pipes[Up].put(append([]byte(nil), b[:n]...))
	}
}

func (r *Impair) readServer(s *net.UDPConn) {
	defer r.wg.Done()
	b := make([]byte, 65536)
	for {
		n, err := s.Read(b)
		if err != nil {
			// A connected UDP socket reads the ICMP of a closed server
			// port as an error; only Close ends the relay.
			if errors.Is(err, net.ErrClosed) {
				return
			}
			continue
		}
		r.pipes[Down].put(append([]byte(nil), b[:n]...))
	}
}

func (r *Impair) sendUp(b []byte) {
	r.mu.Lock()
	s := r.upstreams[len(r.upstreams)-1]
	r.mu.Unlock()
	s.Write(b)
}

func (r *Impair) sendDown(b []byte) {
	r.mu.Lock()
	to := r.client
	r.mu.Unlock()
	if to != nil {
		r.listen.WriteToUDP(b, to)
	}
}

// An impairPipe is one direction of a relay: it decides the fate of each
// packet as it comes, and a goroutine sends the packets it keeps when they
// are due.
type impairPipe struct {
	send func([]byte)
	wake chan struct{}

	mu       sync.Mutex
	shape    Shape
	rnd      *rand.Rand
	counters ImpairCounters
	data     ImpairData
	queue    impairQueue
	// order breaks ties of due time, so packets of one due time keep
	// their order.
	order int
	// held is the packet that waits for the next one, hold the timer that
	// sends it alone, and holds the count of packets held, so a timer that
	// fires late sends only its own packet.
	held    []byte
	hold    *time.Timer
	holds   int
	stopped bool
}

func newImpairPipe(r *Impair, rnd *rand.Rand, send func([]byte)) *impairPipe {
	p := &impairPipe{send: send, rnd: rnd, wake: make(chan struct{}, 1)}
	r.wg.Add(1)
	go p.run(r.done, &r.wg)
	return p
}

// put takes a packet that came in.
func (p *impairPipe) put(b []byte) {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.stopped {
		return
	}
	p.counters.Received++
	data := len(b) >= impairSRTHeader && b[0]&0x80 == 0
	retrans := data && b[4]&0x04 != 0
	if data {
		p.data.Received++
	}
	if retrans {
		p.data.Retrans++
	}
	if p.shape.Blackhole {
		p.counters.Blackholed++
		return
	}
	// Draw every number for every packet, so a change of one setting
	// doesn't move the choices of the others.
	loss, dup, reorder := p.rnd.Float64()*100, p.rnd.Float64()*100, p.rnd.Float64()*100
	jitter := p.rnd.Float64()*2 - 1
	if loss < p.shape.Loss {
		p.counters.Lost++
		if data {
			p.data.Lost++
		}
		if retrans {
			p.data.RetransLost++
		}
		return
	}
	delay := max(0, p.shape.Delay+time.Duration(jitter*float64(p.shape.Jitter)))
	if p.held == nil && reorder < p.shape.Reorder {
		p.counters.Reordered++
		p.held = b
		p.holds++
		id := p.holds
		p.hold = time.AfterFunc(impairHold, func() { p.release(id) })
		return
	}
	p.push(b, delay)
	if dup < p.shape.Duplicate {
		p.counters.Duplicated++
		p.push(b, delay)
	}
	if p.held != nil {
		p.hold.Stop()
		p.push(p.held, delay)
		p.held = nil
	}
}

// release sends the held packet id that no next one came for.
func (p *impairPipe) release(id int) {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.held == nil || p.holds != id || p.stopped {
		return
	}
	p.push(p.held, p.shape.Delay)
	p.held = nil
}

// push queues a packet to send after delay. The caller holds mu.
func (p *impairPipe) push(b []byte, delay time.Duration) {
	heap.Push(&p.queue, impairItem{b: b, due: time.Now().Add(delay), order: p.order})
	p.order++
	select {
	case p.wake <- struct{}{}:
	default:
	}
}

func (p *impairPipe) stop() {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.stopped = true
	if p.hold != nil {
		p.hold.Stop()
	}
}

func (p *impairPipe) run(done chan struct{}, wg *sync.WaitGroup) {
	defer wg.Done()
	timer := time.NewTimer(time.Hour)
	defer timer.Stop()
	for {
		p.mu.Lock()
		var next []byte
		wait := time.Hour
		if len(p.queue) > 0 {
			if d := time.Until(p.queue[0].due); d > 0 {
				wait = d
			} else {
				next = heap.Pop(&p.queue).(impairItem).b
				p.counters.Sent++
			}
		}
		p.mu.Unlock()
		if next != nil {
			p.send(next)
			continue
		}
		timer.Reset(wait)
		select {
		case <-done:
			return
		case <-p.wake:
		case <-timer.C:
		}
	}
}

type impairItem struct {
	b     []byte
	due   time.Time
	order int
}

// impairQueue is a heap of packets, the first due first.
type impairQueue []impairItem

func (q impairQueue) Len() int { return len(q) }
func (q impairQueue) Less(i, j int) bool {
	if q[i].due.Equal(q[j].due) {
		return q[i].order < q[j].order
	}
	return q[i].due.Before(q[j].due)
}
func (q impairQueue) Swap(i, j int) { q[i], q[j] = q[j], q[i] }
func (q *impairQueue) Push(x any)   { *q = append(*q, x.(impairItem)) }
func (q *impairQueue) Pop() any {
	old := *q
	it := old[len(old)-1]
	*q = old[:len(old)-1]
	return it
}
