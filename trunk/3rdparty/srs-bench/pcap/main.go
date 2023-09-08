package main

import (
	"bufio"
	"context"
	"flag"
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
	var doRE, doTrace, help bool
	var pauseNumber, abortNumber uint64
	var filename string
	var server string
	flag.BoolVar(&help, "h", false, "whether show this help")
	flag.BoolVar(&help, "help", false, "whether show this help")
	flag.BoolVar(&doRE, "re", true, "whether do real-time emulation")
	flag.BoolVar(&doTrace, "trace", true, "whether trace the packet")
	flag.Uint64Var(&pauseNumber, "pause", 0, "the packet number to pause")
	flag.Uint64Var(&abortNumber, "abort", 0, "the packet number to abort")
	flag.StringVar(&filename, "f", "", "the pcap filename, like ./t.pcapng")
	flag.StringVar(&server, "s", "", "the server address, like 127.0.0.1:1935")

	flag.Parse()

	if help {
		flag.Usage()
		os.Exit(0)
	}

	if filename == "" || server == "" {
		flag.Usage()
		os.Exit(1)
	}

	logger.Tf(ctx, "Forward pcap %v to %v, re=%v, trace=%v, pause=%v, abort=%v",
		filename, server, doRE, doTrace, pauseNumber, abortNumber)

	f, err := os.Open(filename)
	if err != nil {
		return errors.Wrapf(err, "open pcap %v", filename)
	}
	defer f.Close()

	r, err := pcapgo.NewNgReader(f, pcapgo.DefaultNgReaderOptions)
	if err != nil {
		return errors.Wrapf(err, "new reader")
	}

	// TODO: FIXME: Should start a goroutine to consume bytes from conn.
	conn, err := net.Dial("tcp", server)
	if err != nil {
		return errors.Wrapf(err, "dial %v", server)
	}
	defer conn.Close()

	var packetNumber uint64
	var previousTime *time.Time
	source := gopacket.NewPacketSource(r, r.LinkType())
	for packet := range source.Packets() {
		packetNumber++

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

		if pauseNumber > 0 && packetNumber == pauseNumber {
			reader := bufio.NewReader(os.Stdin)
			trace("#%v Press Enter to continue...", packetNumber)
			_, _ = reader.ReadString('\n')
		}
		if abortNumber > 0 && packetNumber > abortNumber {
			break
		}

		if _, err := conn.Write(payload); err != nil {
			return errors.Wrapf(err, "write to %v", server)
		}

		if doRE {
			if previousTime == nil {
				previousTime = &ci.Timestamp
			} else {
				if diff := ci.Timestamp.Sub(*previousTime); diff > 100*time.Millisecond {
					time.Sleep(diff)
					previousTime = &ci.Timestamp
				}
			}
		}

		if doTrace {
			trace("#%v TCP %v=>%v %v Len:%v",
				packetNumber, uint16(tcp.SrcPort), uint16(tcp.DstPort),
				ci.Timestamp.Format("15:04:05.000"),
				len(payload))
		}
	}

	return nil
}
