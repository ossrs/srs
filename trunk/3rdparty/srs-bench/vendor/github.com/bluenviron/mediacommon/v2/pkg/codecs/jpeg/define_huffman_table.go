package jpeg

// DefineHuffmanTable is a DHT marker.
type DefineHuffmanTable struct {
	Codes       []byte
	Symbols     []byte
	TableNumber int
	TableClass  int
}

// Marshal encodes the marker.
func (m DefineHuffmanTable) Marshal(buf []byte) []byte {
	buf = append(buf, []byte{0xFF, MarkerDefineHuffmanTable}...)
	s := 3 + len(m.Codes) + len(m.Symbols)
	buf = append(buf, []byte{byte(s >> 8), byte(s)}...) // length
	buf = append(buf, []byte{byte(m.TableClass<<4) | byte(m.TableNumber)}...)
	buf = append(buf, m.Codes...)
	buf = append(buf, m.Symbols...)
	return buf
}
