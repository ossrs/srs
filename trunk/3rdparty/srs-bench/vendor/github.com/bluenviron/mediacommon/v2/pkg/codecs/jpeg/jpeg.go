// Package jpeg contains utilities to work with the JPEG codec.
package jpeg

// standard JPEG markers.
const (
	MarkerStartOfImage            = 0xD8
	MarkerDefineQuantizationTable = 0xDB
	MarkerDefineHuffmanTable      = 0xC4
	MarkerDefineRestartInterval   = 0xDD
	MarkerStartOfFrame1           = 0xC0
	MarkerStartOfScan             = 0xDA
	MarkerEndOfImage              = 0xD9
	MarkerComment                 = 0xFE
)
