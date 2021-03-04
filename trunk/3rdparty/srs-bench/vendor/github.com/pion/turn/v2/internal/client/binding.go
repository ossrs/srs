package client

import (
	"net"
	"sync"
	"sync/atomic"
	"time"
)

//  Chanel number:
//    0x4000 through 0x7FFF: These values are the allowed channel
//    numbers (16,383 possible values).
const (
	minChannelNumber uint16 = 0x4000
	maxChannelNumber uint16 = 0x7fff
)

type bindingState int32

const (
	bindingStateIdle bindingState = iota
	bindingStateRequest
	bindingStateReady
	bindingStateRefresh
	bindingStateFailed
)

type binding struct {
	number       uint16          // read-only
	st           bindingState    // thread-safe (atomic op)
	addr         net.Addr        // read-only
	mgr          *bindingManager // read-only
	muBind       sync.Mutex      // thread-safe, for ChannelBind ops
	_refreshedAt time.Time       // protected by mutex
	mutex        sync.RWMutex    // thread-safe
}

func (b *binding) setState(state bindingState) {
	atomic.StoreInt32((*int32)(&b.st), int32(state))
}

func (b *binding) state() bindingState {
	return bindingState(atomic.LoadInt32((*int32)(&b.st)))
}

func (b *binding) setRefreshedAt(at time.Time) {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	b._refreshedAt = at
}

func (b *binding) refreshedAt() time.Time {
	b.mutex.RLock()
	defer b.mutex.RUnlock()

	return b._refreshedAt
}

// Thread-safe binding map
type bindingManager struct {
	chanMap map[uint16]*binding
	addrMap map[string]*binding
	next    uint16
	mutex   sync.RWMutex
}

func newBindingManager() *bindingManager {
	return &bindingManager{
		chanMap: map[uint16]*binding{},
		addrMap: map[string]*binding{},
		next:    minChannelNumber,
	}
}

func (mgr *bindingManager) assignChannelNumber() uint16 {
	n := mgr.next
	if mgr.next == maxChannelNumber {
		mgr.next = minChannelNumber
	} else {
		mgr.next++
	}
	return n
}

func (mgr *bindingManager) create(addr net.Addr) *binding {
	mgr.mutex.Lock()
	defer mgr.mutex.Unlock()

	b := &binding{
		number:       mgr.assignChannelNumber(),
		addr:         addr,
		mgr:          mgr,
		_refreshedAt: time.Now(),
	}

	mgr.chanMap[b.number] = b
	mgr.addrMap[b.addr.String()] = b
	return b
}

func (mgr *bindingManager) findByAddr(addr net.Addr) (*binding, bool) {
	mgr.mutex.RLock()
	defer mgr.mutex.RUnlock()

	b, ok := mgr.addrMap[addr.String()]
	return b, ok
}

func (mgr *bindingManager) findByNumber(number uint16) (*binding, bool) {
	mgr.mutex.RLock()
	defer mgr.mutex.RUnlock()

	b, ok := mgr.chanMap[number]
	return b, ok
}

func (mgr *bindingManager) deleteByAddr(addr net.Addr) bool {
	mgr.mutex.Lock()
	defer mgr.mutex.Unlock()

	b, ok := mgr.addrMap[addr.String()]
	if !ok {
		return false
	}

	delete(mgr.addrMap, addr.String())
	delete(mgr.chanMap, b.number)
	return true
}

func (mgr *bindingManager) deleteByNumber(number uint16) bool {
	mgr.mutex.Lock()
	defer mgr.mutex.Unlock()

	b, ok := mgr.chanMap[number]
	if !ok {
		return false
	}

	delete(mgr.addrMap, b.addr.String())
	delete(mgr.chanMap, number)
	return true
}

func (mgr *bindingManager) size() int {
	mgr.mutex.RLock()
	defer mgr.mutex.RUnlock()

	return len(mgr.chanMap)
}
