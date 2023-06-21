package sctp

const (
	paddingMultiple = 4
)

func getPadding(l int) int {
	return (paddingMultiple - (l % paddingMultiple)) % paddingMultiple
}

func padByte(in []byte, cnt int) []byte {
	if cnt < 0 {
		cnt = 0
	}
	padding := make([]byte, cnt)
	return append(in, padding...)
}

// Serial Number Arithmetic (RFC 1982)
func sna32LT(i1, i2 uint32) bool {
	return (i1 < i2 && i2-i1 < 1<<31) || (i1 > i2 && i1-i2 > 1<<31)
}

func sna32LTE(i1, i2 uint32) bool {
	return i1 == i2 || sna32LT(i1, i2)
}

func sna32GT(i1, i2 uint32) bool {
	return (i1 < i2 && (i2-i1) >= 1<<31) || (i1 > i2 && (i1-i2) <= 1<<31)
}

func sna32GTE(i1, i2 uint32) bool {
	return i1 == i2 || sna32GT(i1, i2)
}

func sna32EQ(i1, i2 uint32) bool {
	return i1 == i2
}

func sna16LT(i1, i2 uint16) bool {
	return (i1 < i2 && (i2-i1) < 1<<15) || (i1 > i2 && (i1-i2) > 1<<15)
}

func sna16LTE(i1, i2 uint16) bool {
	return i1 == i2 || sna16LT(i1, i2)
}

func sna16GT(i1, i2 uint16) bool {
	return (i1 < i2 && (i2-i1) >= 1<<15) || (i1 > i2 && (i1-i2) <= 1<<15)
}

func sna16GTE(i1, i2 uint16) bool {
	return i1 == i2 || sna16GT(i1, i2)
}

func sna16EQ(i1, i2 uint16) bool {
	return i1 == i2
}
