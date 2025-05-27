// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package twcc

const (
	minCapacity        = 128
	maxNumberOfPackets = 1 << 15
)

// packetArrivalTimeMap is adapted from Chrome's implementation of TWCC, and keeps track
// of the arrival times of packets. It is used by the TWCC interceptor to build feedback
// packets.
// See https://source.chromium.org/chromium/chromium/src/+/refs/heads/main:third_party/webrtc/modules/remote_bitrate_estimator/packet_arrival_map.h;drc=b5cd13bb6d5d157a5fbe3628b2dd1c1e106203c6
type packetArrivalTimeMap struct {
	// arrivalTimes is a circular buffer, where the packet with sequence number sn is stored
	// in slot sn % len(arrivalTimes)
	arrivalTimes []int64

	// The unwrapped sequence numbers for the range of valid sequence numbers in arrivalTimes.
	// beginSequenceNumber is inclusive, and endSequenceNumber is exclusive.
	beginSequenceNumber, endSequenceNumber int64
}

// AddPacket records the fact that the packet with sequence number sequenceNumber arrived
// at arrivalTime.
func (m *packetArrivalTimeMap) AddPacket(sequenceNumber int64, arrivalTime int64) {
	if m.arrivalTimes == nil {
		// First packet
		m.reallocate(minCapacity)
		m.beginSequenceNumber = sequenceNumber
		m.endSequenceNumber = sequenceNumber + 1
		m.arrivalTimes[m.index(sequenceNumber)] = arrivalTime
		return
	}

	if sequenceNumber >= m.beginSequenceNumber && sequenceNumber < m.endSequenceNumber {
		// The packet is within the buffer, no need to resize.
		m.arrivalTimes[m.index(sequenceNumber)] = arrivalTime
		return
	}

	if sequenceNumber < m.beginSequenceNumber {
		// The packet goes before the current buffer. Expand to add packet,
		// but only if it fits within the maximum number of packets.
		newSize := int(m.endSequenceNumber - sequenceNumber)
		if newSize > maxNumberOfPackets {
			// Don't expand the buffer back for this packet, as it would remove newer received
			// packets.
			return
		}
		m.adjustToSize(newSize)
		m.arrivalTimes[m.index(sequenceNumber)] = arrivalTime
		m.setNotReceived(sequenceNumber+1, m.beginSequenceNumber)
		m.beginSequenceNumber = sequenceNumber
		return
	}

	// The packet goes after the buffer.
	newEndSequenceNumber := sequenceNumber + 1

	if newEndSequenceNumber >= m.endSequenceNumber+maxNumberOfPackets {
		// All old packets have to be removed.
		m.beginSequenceNumber = sequenceNumber
		m.endSequenceNumber = newEndSequenceNumber
		m.arrivalTimes[m.index(sequenceNumber)] = arrivalTime
		return
	}

	if m.beginSequenceNumber < newEndSequenceNumber-maxNumberOfPackets {
		// Remove oldest entries.
		m.beginSequenceNumber = newEndSequenceNumber - maxNumberOfPackets
	}

	m.adjustToSize(int(newEndSequenceNumber - m.beginSequenceNumber))

	// Packets can be received out of order. If this isn't the next expected packet,
	// add enough placeholders to fill the gap.
	m.setNotReceived(m.endSequenceNumber, sequenceNumber)
	m.endSequenceNumber = newEndSequenceNumber
	m.arrivalTimes[m.index(sequenceNumber)] = arrivalTime
}

func (m *packetArrivalTimeMap) setNotReceived(startInclusive, endExclusive int64) {
	for sn := startInclusive; sn < endExclusive; sn++ {
		m.arrivalTimes[m.index(sn)] = -1
	}
}

// BeginSequenceNumber returns the first valid sequence number in the map.
func (m *packetArrivalTimeMap) BeginSequenceNumber() int64 {
	return m.beginSequenceNumber
}

// EndSequenceNumber returns the first sequence number after the last valid sequence number in the map.
func (m *packetArrivalTimeMap) EndSequenceNumber() int64 {
	return m.endSequenceNumber
}

// FindNextAtOrAfter returns the sequence number and timestamp of the first received packet that has a sequence number
// greator or equal to sequenceNumber.
func (m *packetArrivalTimeMap) FindNextAtOrAfter(sequenceNumber int64) (foundSequenceNumber int64, arrivalTime int64, ok bool) {
	for sequenceNumber = m.Clamp(sequenceNumber); sequenceNumber < m.endSequenceNumber; sequenceNumber++ {
		if t := m.get(sequenceNumber); t >= 0 {
			return sequenceNumber, t, true
		}
	}
	return -1, -1, false
}

// EraseTo erases all elements from the beginning of the map until sequenceNumber.
func (m *packetArrivalTimeMap) EraseTo(sequenceNumber int64) {
	if sequenceNumber < m.beginSequenceNumber {
		return
	}
	if sequenceNumber >= m.endSequenceNumber {
		// Erase all.
		m.beginSequenceNumber = m.endSequenceNumber
		return
	}
	// Remove some
	m.beginSequenceNumber = sequenceNumber
	m.adjustToSize(int(m.endSequenceNumber - m.beginSequenceNumber))
}

// RemoveOldPackets removes packets from the beginning of the map as long as they are before
// sequenceNumber and with an age older than arrivalTimeLimit.
func (m *packetArrivalTimeMap) RemoveOldPackets(sequenceNumber int64, arrivalTimeLimit int64) {
	checkTo := min64(sequenceNumber, m.endSequenceNumber)
	for m.beginSequenceNumber < checkTo && m.get(m.beginSequenceNumber) <= arrivalTimeLimit {
		m.beginSequenceNumber++
	}
	m.adjustToSize(int(m.endSequenceNumber - m.beginSequenceNumber))
}

// HasReceived returns whether a packet with the sequence number has been received.
func (m *packetArrivalTimeMap) HasReceived(sequenceNumber int64) bool {
	return m.get(sequenceNumber) >= 0
}

// Clamp returns sequenceNumber clamped to [beginSequenceNumber, endSequenceNumber]
func (m *packetArrivalTimeMap) Clamp(sequenceNumber int64) int64 {
	if sequenceNumber < m.beginSequenceNumber {
		return m.beginSequenceNumber
	}
	if m.endSequenceNumber < sequenceNumber {
		return m.endSequenceNumber
	}
	return sequenceNumber
}

func (m *packetArrivalTimeMap) get(sequenceNumber int64) int64 {
	if sequenceNumber < m.beginSequenceNumber || sequenceNumber >= m.endSequenceNumber {
		return -1
	}
	return m.arrivalTimes[m.index(sequenceNumber)]
}

func (m *packetArrivalTimeMap) index(sequenceNumber int64) int {
	// Sequence number might be negative, and we always guarantee that arrivalTimes
	// length is a power of 2, so it's easier to use "&" instead of "%"
	return int(sequenceNumber & int64(m.capacity()-1))
}

func (m *packetArrivalTimeMap) adjustToSize(newSize int) {
	if newSize > m.capacity() {
		newCapacity := m.capacity()
		for newCapacity < newSize {
			newCapacity *= 2
		}
		m.reallocate(newCapacity)
	}
	if m.capacity() > maxInt(minCapacity, newSize*4) {
		newCapacity := m.capacity()
		for newCapacity >= 2*maxInt(newSize, minCapacity) {
			newCapacity /= 2
		}
		m.reallocate(newCapacity)
	}
}

func (m *packetArrivalTimeMap) capacity() int {
	return len(m.arrivalTimes)
}

func (m *packetArrivalTimeMap) reallocate(newCapacity int) {
	newBuffer := make([]int64, newCapacity)
	for sn := m.beginSequenceNumber; sn < m.endSequenceNumber; sn++ {
		newBuffer[int(sn&(int64(newCapacity-1)))] = m.get(sn)
	}
	m.arrivalTimes = newBuffer
}
