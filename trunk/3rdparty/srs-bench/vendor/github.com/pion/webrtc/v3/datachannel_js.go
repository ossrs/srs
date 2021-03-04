// +build js,wasm

package webrtc

import (
	"fmt"
	"syscall/js"

	"github.com/pion/datachannel"
)

const dataChannelBufferSize = 16384 // Lowest common denominator among browsers

// DataChannel represents a WebRTC DataChannel
// The DataChannel interface represents a network channel
// which can be used for bidirectional peer-to-peer transfers of arbitrary data
type DataChannel struct {
	// Pointer to the underlying JavaScript RTCPeerConnection object.
	underlying js.Value

	// Keep track of handlers/callbacks so we can call Release as required by the
	// syscall/js API. Initially nil.
	onOpenHandler       *js.Func
	onCloseHandler      *js.Func
	onMessageHandler    *js.Func
	onBufferedAmountLow *js.Func

	// A reference to the associated api object used by this datachannel
	api *API
}

// OnOpen sets an event handler which is invoked when
// the underlying data transport has been established (or re-established).
func (d *DataChannel) OnOpen(f func()) {
	if d.onOpenHandler != nil {
		oldHandler := d.onOpenHandler
		defer oldHandler.Release()
	}
	onOpenHandler := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		go f()
		return js.Undefined()
	})
	d.onOpenHandler = &onOpenHandler
	d.underlying.Set("onopen", onOpenHandler)
}

// OnClose sets an event handler which is invoked when
// the underlying data transport has been closed.
func (d *DataChannel) OnClose(f func()) {
	if d.onCloseHandler != nil {
		oldHandler := d.onCloseHandler
		defer oldHandler.Release()
	}
	onCloseHandler := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		go f()
		return js.Undefined()
	})
	d.onCloseHandler = &onCloseHandler
	d.underlying.Set("onclose", onCloseHandler)
}

// OnMessage sets an event handler which is invoked on a binary message arrival
// from a remote peer. Note that browsers may place limitations on message size.
func (d *DataChannel) OnMessage(f func(msg DataChannelMessage)) {
	if d.onMessageHandler != nil {
		oldHandler := d.onMessageHandler
		defer oldHandler.Release()
	}
	onMessageHandler := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		// pion/webrtc/projects/15
		data := args[0].Get("data")
		go func() {
			// valueToDataChannelMessage may block when handling 'Blob' data
			// so we need to call it from a new routine. See:
			// https://pkg.go.dev/syscall/js#FuncOf
			msg := valueToDataChannelMessage(data)
			f(msg)
		}()
		return js.Undefined()
	})
	d.onMessageHandler = &onMessageHandler
	d.underlying.Set("onmessage", onMessageHandler)
}

// Send sends the binary message to the DataChannel peer
func (d *DataChannel) Send(data []byte) (err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()
	array := js.Global().Get("Uint8Array").New(len(data))
	js.CopyBytesToJS(array, data)
	d.underlying.Call("send", array)
	return nil
}

// SendText sends the text message to the DataChannel peer
func (d *DataChannel) SendText(s string) (err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()
	d.underlying.Call("send", s)
	return nil
}

// Detach allows you to detach the underlying datachannel. This provides
// an idiomatic API to work with, however it disables the OnMessage callback.
// Before calling Detach you have to enable this behavior by calling
// webrtc.DetachDataChannels(). Combining detached and normal data channels
// is not supported.
// Please reffer to the data-channels-detach example and the
// pion/datachannel documentation for the correct way to handle the
// resulting DataChannel object.
func (d *DataChannel) Detach() (datachannel.ReadWriteCloser, error) {
	if !d.api.settingEngine.detach.DataChannels {
		return nil, fmt.Errorf("enable detaching by calling webrtc.DetachDataChannels()")
	}

	detached := newDetachedDataChannel(d)
	return detached, nil
}

// Close Closes the DataChannel. It may be called regardless of whether
// the DataChannel object was created by this peer or the remote peer.
func (d *DataChannel) Close() (err error) {
	defer func() {
		if e := recover(); e != nil {
			err = recoveryToError(e)
		}
	}()

	d.underlying.Call("close")

	// Release any handlers as required by the syscall/js API.
	if d.onOpenHandler != nil {
		d.onOpenHandler.Release()
	}
	if d.onCloseHandler != nil {
		d.onCloseHandler.Release()
	}
	if d.onMessageHandler != nil {
		d.onMessageHandler.Release()
	}
	if d.onBufferedAmountLow != nil {
		d.onBufferedAmountLow.Release()
	}

	return nil
}

// Label represents a label that can be used to distinguish this
// DataChannel object from other DataChannel objects. Scripts are
// allowed to create multiple DataChannel objects with the same label.
func (d *DataChannel) Label() string {
	return d.underlying.Get("label").String()
}

// Ordered represents if the DataChannel is ordered, and false if
// out-of-order delivery is allowed.
func (d *DataChannel) Ordered() bool {
	ordered := d.underlying.Get("ordered")
	if jsValueIsUndefined(ordered) {
		return true // default is true
	}
	return ordered.Bool()
}

// MaxPacketLifeTime represents the length of the time window (msec) during
// which transmissions and retransmissions may occur in unreliable mode.
func (d *DataChannel) MaxPacketLifeTime() *uint16 {
	if !jsValueIsUndefined(d.underlying.Get("maxPacketLifeTime")) {
		return valueToUint16Pointer(d.underlying.Get("maxPacketLifeTime"))
	} else {
		// See https://bugs.chromium.org/p/chromium/issues/detail?id=696681
		// Chrome calls this "maxRetransmitTime"
		return valueToUint16Pointer(d.underlying.Get("maxRetransmitTime"))
	}
}

// MaxRetransmits represents the maximum number of retransmissions that are
// attempted in unreliable mode.
func (d *DataChannel) MaxRetransmits() *uint16 {
	return valueToUint16Pointer(d.underlying.Get("maxRetransmits"))
}

// Protocol represents the name of the sub-protocol used with this
// DataChannel.
func (d *DataChannel) Protocol() string {
	return d.underlying.Get("protocol").String()
}

// Negotiated represents whether this DataChannel was negotiated by the
// application (true), or not (false).
func (d *DataChannel) Negotiated() bool {
	return d.underlying.Get("negotiated").Bool()
}

// ID represents the ID for this DataChannel. The value is initially
// null, which is what will be returned if the ID was not provided at
// channel creation time. Otherwise, it will return the ID that was either
// selected by the script or generated. After the ID is set to a non-null
// value, it will not change.
func (d *DataChannel) ID() *uint16 {
	return valueToUint16Pointer(d.underlying.Get("id"))
}

// ReadyState represents the state of the DataChannel object.
func (d *DataChannel) ReadyState() DataChannelState {
	return newDataChannelState(d.underlying.Get("readyState").String())
}

// BufferedAmount represents the number of bytes of application data
// (UTF-8 text and binary data) that have been queued using send(). Even
// though the data transmission can occur in parallel, the returned value
// MUST NOT be decreased before the current task yielded back to the event
// loop to prevent race conditions. The value does not include framing
// overhead incurred by the protocol, or buffering done by the operating
// system or network hardware. The value of BufferedAmount slot will only
// increase with each call to the send() method as long as the ReadyState is
// open; however, BufferedAmount does not reset to zero once the channel
// closes.
func (d *DataChannel) BufferedAmount() uint64 {
	return uint64(d.underlying.Get("bufferedAmount").Int())
}

// BufferedAmountLowThreshold represents the threshold at which the
// bufferedAmount is considered to be low. When the bufferedAmount decreases
// from above this threshold to equal or below it, the bufferedamountlow
// event fires. BufferedAmountLowThreshold is initially zero on each new
// DataChannel, but the application may change its value at any time.
func (d *DataChannel) BufferedAmountLowThreshold() uint64 {
	return uint64(d.underlying.Get("bufferedAmountLowThreshold").Int())
}

// SetBufferedAmountLowThreshold is used to update the threshold.
// See BufferedAmountLowThreshold().
func (d *DataChannel) SetBufferedAmountLowThreshold(th uint64) {
	d.underlying.Set("bufferedAmountLowThreshold", th)
}

// OnBufferedAmountLow sets an event handler which is invoked when
// the number of bytes of outgoing data becomes lower than the
// BufferedAmountLowThreshold.
func (d *DataChannel) OnBufferedAmountLow(f func()) {
	if d.onBufferedAmountLow != nil {
		oldHandler := d.onBufferedAmountLow
		defer oldHandler.Release()
	}
	onBufferedAmountLow := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		go f()
		return js.Undefined()
	})
	d.onBufferedAmountLow = &onBufferedAmountLow
	d.underlying.Set("onbufferedamountlow", onBufferedAmountLow)
}

// valueToDataChannelMessage converts the given value to a DataChannelMessage.
// val should be obtained from MessageEvent.data where MessageEvent is received
// via the RTCDataChannel.onmessage callback.
func valueToDataChannelMessage(val js.Value) DataChannelMessage {
	// If val is of type string, the conversion is straightforward.
	if val.Type() == js.TypeString {
		return DataChannelMessage{
			IsString: true,
			Data:     []byte(val.String()),
		}
	}

	// For other types, we need to first determine val.constructor.name.
	constructorName := val.Get("constructor").Get("name").String()
	var data []byte
	switch constructorName {
	case "Uint8Array":
		// We can easily convert Uint8Array to []byte
		data = uint8ArrayValueToBytes(val)
	case "Blob":
		// Convert the Blob to an ArrayBuffer and then convert the ArrayBuffer
		// to a Uint8Array.
		// See: https://developer.mozilla.org/en-US/docs/Web/API/Blob

		// The JavaScript API for reading from the Blob is asynchronous. We use a
		// channel to signal when reading is done.
		reader := js.Global().Get("FileReader").New()
		doneChan := make(chan struct{})
		reader.Call("addEventListener", "loadend", js.FuncOf(func(this js.Value, args []js.Value) interface{} {
			go func() {
				// Signal that the FileReader is done reading/loading by sending through
				// the doneChan.
				doneChan <- struct{}{}
			}()
			return js.Undefined()
		}))

		reader.Call("readAsArrayBuffer", val)

		// Wait for the FileReader to finish reading/loading.
		<-doneChan

		// At this point buffer.result is a typed array, which we know how to
		// handle.
		buffer := reader.Get("result")
		uint8Array := js.Global().Get("Uint8Array").New(buffer)
		data = uint8ArrayValueToBytes(uint8Array)
	default:
		// Assume we have an ArrayBufferView type which we can convert to a
		// Uint8Array in JavaScript.
		// See: https://developer.mozilla.org/en-US/docs/Web/API/ArrayBufferView
		uint8Array := js.Global().Get("Uint8Array").New(val)
		data = uint8ArrayValueToBytes(uint8Array)
	}

	return DataChannelMessage{
		IsString: false,
		Data:     data,
	}
}
