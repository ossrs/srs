package sctp

// control queue

type controlQueue struct {
	queue []*packet
}

func newControlQueue() *controlQueue {
	return &controlQueue{queue: []*packet{}}
}

func (q *controlQueue) push(c *packet) {
	q.queue = append(q.queue, c)
}

func (q *controlQueue) pushAll(packets []*packet) {
	q.queue = append(q.queue, packets...)
}

func (q *controlQueue) popAll() []*packet {
	packets := q.queue
	q.queue = []*packet{}
	return packets
}

func (q *controlQueue) size() int {
	return len(q.queue)
}
