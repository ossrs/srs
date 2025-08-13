// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtcp

import (
	"fmt"
	"reflect"
)

/*
Converts an RTCP Packet into a human-readable format. The Packets
themselves can control the presentation as follows:

  - Fields of a type that have a String() method will be formatted
    with that String method (which should not emit '\n' characters)

  - Otherwise, fields with a tag containing a "fmt" string will use that
    format when serializing the value. For example, to format an SSRC
    value as base 16 insted of base 10:

    type ExamplePacket struct {
    LocalSSRC   uint32   `fmt:"0x%X"`
    RemotsSSRCs []uint32 `fmt:"%X"`
    }

- If no fmt string is present, "%+v" is used by default

The intention of this stringify() function is to simplify creation
of String() methods on new packet types, as it provides a simple
baseline implementation that works well in the majority of cases.
*/
func stringify(p Packet) string {
	value := reflect.Indirect(reflect.ValueOf(p))
	return formatField(value.Type().String(), "", p, "")
}

func formatField(name string, format string, f interface{}, indent string) string { //nolint:gocognit
	out := indent
	value := reflect.ValueOf(f)

	if !value.IsValid() {
		return fmt.Sprintf("%s%s: <nil>\n", out, name)
	}

	isPacket := reflect.TypeOf(f).Implements(reflect.TypeOf((*Packet)(nil)).Elem())

	// Resolve pointers to their underlying values
	if value.Type().Kind() == reflect.Ptr && !value.IsNil() {
		underlying := reflect.Indirect(value)
		if underlying.IsValid() {
			value = underlying
		}
	}

	// If the field type has a custom String method, use that
	// (unless we're a packet, since we want to avoid recursing
	// back into this function if the Packet's String() method
	// uses it)
	if stringMethod := value.MethodByName("String"); !isPacket && stringMethod.IsValid() {
		out += fmt.Sprintf("%s: %s\n", name, stringMethod.Call([]reflect.Value{}))
		return out
	}

	switch value.Kind() {
	case reflect.Struct:
		out += fmt.Sprintf("%s:\n", name)
		for i := 0; i < value.NumField(); i++ {
			if value.Field(i).CanInterface() {
				format = value.Type().Field(i).Tag.Get("fmt")
				if format == "" {
					format = "%+v"
				}
				out += formatField(value.Type().Field(i).Name, format, value.Field(i).Interface(), indent+"\t")
			}
		}
	case reflect.Slice:
		childKind := value.Type().Elem().Kind()
		_, hasStringMethod := value.Type().Elem().MethodByName("String")
		if hasStringMethod || childKind == reflect.Struct || childKind == reflect.Ptr || childKind == reflect.Interface || childKind == reflect.Slice {
			out += fmt.Sprintf("%s:\n", name)
			for i := 0; i < value.Len(); i++ {
				childName := fmt.Sprint(i)
				// Since interfaces can hold different types of things, we add the
				// most specific type name to the name to make it clear what the
				// subsequent fields represent.
				if value.Index(i).Kind() == reflect.Interface {
					childName += fmt.Sprintf(" (%s)", reflect.Indirect(reflect.ValueOf(value.Index(i).Interface())).Type())
				}
				if value.Index(i).CanInterface() {
					out += formatField(childName, format, value.Index(i).Interface(), indent+"\t")
				}
			}
			return out
		}

		// If we didn't take care of stringing the value already, we fall through to the
		// generic case. This will print slices of basic types on a single line.
		fallthrough
	default:
		if value.CanInterface() {
			out += fmt.Sprintf("%s: "+format+"\n", name, value.Interface())
		}
	}
	return out
}
