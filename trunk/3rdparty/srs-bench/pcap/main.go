package main

import (
	"bufio"
	"context"
	"fmt"
	"net"
	"os"
	"time"

	"github.com/google/gopacket"
	"github.com/google/gopacket/layers"
	"github.com/google/gopacket/pcapgo"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
)

func main() {
	ctx := logger.WithContext(context.Background())
	if err := doMain(ctx); err != nil {
		panic(err)
	}
}

func trace(format string, args ...interface{}) {
	fmt.Println(fmt.Sprintf(format, args...))
}

func doMain(ctx context.Context) error {
	filename := "/Users/video/Downloads/t2.pcapng"
	address := "127.0.0.1:1935"
	logger.Tf(ctx, "Forward pcap %v to %v", filename, address)

	f, err := os.Open(filename)
	if err != nil {
		return errors.Wrapf(err, "open pcap %v", filename)
	}
	defer f.Close()

	r, err := pcapgo.NewNgReader(f, pcapgo.DefaultNgReaderOptions)
	if err != nil {
		return errors.Wrapf(err, "new reader")
	}

	conn, err := net.Dial("tcp", address)
	if err != nil {
		return errors.Wrapf(err, "dial %v", address)
	}
	defer conn.Close()

	source := gopacket.NewPacketSource(r, r.LinkType())
	var printIndex uint64
	var previousTime *time.Time
	var doRE, doTrace bool
	var pauseIndex, cancelIndex uint64
	for packet := range source.Packets() {
		printIndex++

		if packet.Layer(layers.LayerTypeTCP) == nil {
			continue
		}

		ci := packet.Metadata().CaptureInfo
		tcp, _ := packet.Layer(layers.LayerTypeTCP).(*layers.TCP)
		payload := tcp.Payload
		if len(payload) == 0 {
			continue
		}
		if tcp.DstPort != 1935 {
			continue
		}

		if pauseIndex > 0 && printIndex == pauseIndex {
			reader := bufio.NewReader(os.Stdin)
			trace("#%v Press Enter to continue...", printIndex)
			_, _ = reader.ReadString('\n')
		}
		if cancelIndex > 0 && printIndex > cancelIndex {
			break
		}

		if _, err := conn.Write(payload); err != nil {
			return errors.Wrapf(err, "write to %v", address)
		}

		if doRE {
			if previousTime == nil {
				previousTime = &ci.Timestamp
			} else if diff := ci.Timestamp.Sub(*previousTime); diff > 0 {
				time.Sleep(diff)
			}
		}

		if doTrace {
			trace("#%v TCP %v=>%v %v Len:%v",
				printIndex, uint16(tcp.SrcPort), uint16(tcp.DstPort),
				ci.Timestamp.Format("15:04:05.000"),
				len(payload))
		}
	}

	return nil
}
