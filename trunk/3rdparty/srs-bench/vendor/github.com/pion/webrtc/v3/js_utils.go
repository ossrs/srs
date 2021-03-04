// +build js,wasm

package webrtc

import (
	"fmt"
	"syscall/js"
)

// awaitPromise accepts a js.Value representing a Promise. If the promise
// resolves, it returns (result, nil). If the promise rejects, it returns
// (js.Undefined, error). awaitPromise has a synchronous-like API but does not
// block the JavaScript event loop.
func awaitPromise(promise js.Value) (js.Value, error) {
	resultsChan := make(chan js.Value)
	errChan := make(chan js.Error)

	thenFunc := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		go func() {
			resultsChan <- args[0]
		}()
		return js.Undefined()
	})
	defer thenFunc.Release()
	promise.Call("then", thenFunc)

	catchFunc := js.FuncOf(func(this js.Value, args []js.Value) interface{} {
		go func() {
			errChan <- js.Error{args[0]}
		}()
		return js.Undefined()
	})
	defer catchFunc.Release()
	promise.Call("catch", catchFunc)

	select {
	case result := <-resultsChan:
		return result, nil
	case err := <-errChan:
		return js.Undefined(), err
	}
}

func valueToUint16Pointer(val js.Value) *uint16 {
	if jsValueIsNull(val) || jsValueIsUndefined(val) {
		return nil
	}
	convertedVal := uint16(val.Int())
	return &convertedVal
}

func valueToStringPointer(val js.Value) *string {
	if jsValueIsNull(val) || jsValueIsUndefined(val) {
		return nil
	}
	stringVal := val.String()
	return &stringVal
}

func stringToValueOrUndefined(val string) js.Value {
	if val == "" {
		return js.Undefined()
	}
	return js.ValueOf(val)
}

func uint8ToValueOrUndefined(val uint8) js.Value {
	if val == 0 {
		return js.Undefined()
	}
	return js.ValueOf(val)
}

func interfaceToValueOrUndefined(val interface{}) js.Value {
	if val == nil {
		return js.Undefined()
	}
	return js.ValueOf(val)
}

func valueToStringOrZero(val js.Value) string {
	if jsValueIsUndefined(val) || jsValueIsNull(val) {
		return ""
	}
	return val.String()
}

func valueToUint8OrZero(val js.Value) uint8 {
	if jsValueIsUndefined(val) || jsValueIsNull(val) {
		return 0
	}
	return uint8(val.Int())
}

func valueToUint16OrZero(val js.Value) uint16 {
	if jsValueIsNull(val) || jsValueIsUndefined(val) {
		return 0
	}
	return uint16(val.Int())
}

func valueToUint32OrZero(val js.Value) uint32 {
	if jsValueIsNull(val) || jsValueIsUndefined(val) {
		return 0
	}
	return uint32(val.Int())
}

func valueToStrings(val js.Value) []string {
	result := make([]string, val.Length())
	for i := 0; i < val.Length(); i++ {
		result[i] = val.Index(i).String()
	}
	return result
}

func stringPointerToValue(val *string) js.Value {
	if val == nil {
		return js.Undefined()
	}
	return js.ValueOf(*val)
}

func uint16PointerToValue(val *uint16) js.Value {
	if val == nil {
		return js.Undefined()
	}
	return js.ValueOf(*val)
}

func boolPointerToValue(val *bool) js.Value {
	if val == nil {
		return js.Undefined()
	}
	return js.ValueOf(*val)
}

func stringsToValue(strings []string) js.Value {
	val := make([]interface{}, len(strings))
	for i, s := range strings {
		val[i] = s
	}
	return js.ValueOf(val)
}

func stringEnumToValueOrUndefined(s string) js.Value {
	if s == "unknown" {
		return js.Undefined()
	}
	return js.ValueOf(s)
}

// Converts the return value of recover() to an error.
func recoveryToError(e interface{}) error {
	switch e := e.(type) {
	case error:
		return e
	default:
		return fmt.Errorf("recovered with non-error value: (%T) %s", e, e)
	}
}

func uint8ArrayValueToBytes(val js.Value) []byte {
	result := make([]byte, val.Length())
	js.CopyBytesToGo(result, val)

	return result
}
