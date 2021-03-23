<h1 align="center">
  <br>
  Pion STUN
  <br>
</h1>
<h4 align="center">A Go implementation of STUN</h4>
<p align="center">
  <a href="https://pion.ly"><img src="https://img.shields.io/badge/pion-stun-gray.svg?longCache=true&colorB=brightgreen" alt="Pion stun"></a>
  <!--<a href="https://sourcegraph.com/github.com/pion/webrtc?badge"><img src="https://sourcegraph.com/github.com/pion/webrtc/-/badge.svg" alt="Sourcegraph Widget"></a>-->
  <a href="https://pion.ly/slack"><img src="https://img.shields.io/badge/join-us%20on%20slack-gray.svg?longCache=true&logo=slack&colorB=brightgreen" alt="Slack Widget"></a>
  <br>
  <a href="https://travis-ci.org/pion/stun"><img src="https://travis-ci.org/pion/stun.svg?branch=master" alt="Build Status"></a>
  <a href="https://pkg.go.dev/github.com/pion/stun"><img src="https://godoc.org/github.com/pion/stun?status.svg" alt="GoDoc"></a>
  <a href="https://codecov.io/gh/pion/stun"><img src="https://codecov.io/gh/pion/stun/branch/master/graph/badge.svg" alt="Coverage Status"></a>
  <a href="https://goreportcard.com/report/github.com/pion/stun"><img src="https://goreportcard.com/badge/github.com/pion/stun" alt="Go Report Card"></a>
  <!--<a href="https://www.codacy.com/app/Sean-Der/webrtc"><img src="https://api.codacy.com/project/badge/Grade/18f4aec384894e6aac0b94effe51961d" alt="Codacy Badge"></a>-->
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>
<br>

### Roadmap
The library is used as a part of our WebRTC implementation. Please refer to that [roadmap](https://github.com/pion/webrtc/issues/9) to track our major milestones.

### Community
Pion has an active community on the [Golang Slack](https://invite.slack.golangbridge.org/). Sign up and join the **#pion** channel for discussions and support. You can also use [Pion mailing list](https://groups.google.com/forum/#!forum/pion).

We are always looking to support **your projects**. Please reach out if you have something to build!

If you need commercial support or don't want to use public methods you can contact us at [team@pion.ly](mailto:team@pion.ly)

### Contributing
Check out the **[contributing wiki](https://github.com/pion/webrtc/wiki/Contributing)** to join the group of amazing people making this project possible:

* [Sean DuBois](https://github.com/Sean-Der) - *Original Author*
* [Raphael Randschau](https://github.com/nicolai86) - *STUN client*
* [Michiel De Backker](https://github.com/backkem) - *Minor fixes*
* [Y.Horie](https://github.com/u5surf) - *Fix lint issues*
* [Aleksandr Razumov](https://github.com/ernado) - *The v0.3 version*
* [songjiayang](https://github.com/songjiayang)
* [Adam Kiss](https://github.com/masterada)
* [Moises Marangoni](https://github.com/Moisesbr)
* [Yutaka Takeda](https://github.com/enobufs)
* [Hugo Arregui](https://github.com/hugoArregui)
* [Maanas Royy](https://github.com/maanas)
* [Atsushi Watanabe](https://github.com/at-wat)
* [Cecylia Bocovich](https://github.com/cohosh)
* [Christian Muehlhaeuser](https://github.com/muesli)

# STUN
Package stun implements Session Traversal Utilities for NAT (STUN) [[RFC5389](https://tools.ietf.org/html/rfc5389)]
protocol and [client](https://pkg.go.dev/github.com/pion/stun#Client) with no external dependencies and zero allocations in hot paths.
Client [supports](https://pkg.go.dev/github.com/pion/stun#WithRTO) automatic request retransmissions.

# Example
You can get your current IP address from any STUN server by sending
binding request. See more idiomatic example at `cmd/stun-client`.
```go
package main

import (
	"fmt"

	"github.com/pion/stun"
)

func main() {
	// Creating a "connection" to STUN server.
	c, err := stun.Dial("udp", "stun.l.google.com:19302")
	if err != nil {
		panic(err)
	}
	// Building binding request with random transaction id.
	message := stun.MustBuild(stun.TransactionID, stun.BindingRequest)
	// Sending request to STUN server, waiting for response message.
	if err := c.Do(message, func(res stun.Event) {
		if res.Error != nil {
			panic(res.Error)
		}
		// Decoding XOR-MAPPED-ADDRESS attribute from message.
		var xorAddr stun.XORMappedAddress
		if err := xorAddr.GetFrom(res.Message); err != nil {
			panic(err)
		}
		fmt.Println("your IP is", xorAddr.IP)
	}); err != nil {
		panic(err)
	}
}
```

## Supported RFCs
- [x] [RFC 5389](https://tools.ietf.org/html/rfc5389) — Session Traversal Utilities for NAT
- [x] [RFC 5769](https://tools.ietf.org/html/rfc5769) — Test Vectors for STUN
- [x] [RFC 6062](https://tools.ietf.org/html/rfc6062) — TURN extensions for TCP allocations
- [x] [RFC 7064](https://tools.ietf.org/html/rfc7064) — STUN URI
- [x] (TLS-over-)TCP client support
- [ ] [ALTERNATE-SERVER](https://tools.ietf.org/html/rfc5389#section-11) support [#48](https://github.com/pion/stun/issues/48)
- [ ] [RFC 5780](https://tools.ietf.org/html/rfc5780) — NAT Behavior Discovery Using STUN [#49](https://github.com/pion/stun/issues/49)

# Stability
Package is currently stable, no backward incompatible changes are expected
with exception of critical bugs or security fixes.

Additional attributes are unlikely to be implemented in scope of stun package,
the only exception is constants for attribute or message types.

# RFC 3489 notes
RFC 5389 obsoletes RFC 3489, so implementation was ignored by purpose, however,
RFC 3489 can be easily implemented as separate package.

# Requirements
Go 1.12 is currently supported and tested in CI.

# Testing
Client behavior is tested and verified in many ways:
  * End-To-End with long-term credentials
    * **coturn**: The coturn [server](https://github.com/coturn/coturn/wiki/turnserver) (linux)
  * Bunch of code static checkers (linters)
  * Standard unit-tests with coverage reporting (linux {amd64, **arm**64}, windows and darwin)
  * Explicit API backward compatibility [check](https://github.com/gortc/api), see `api` directory

See [TeamCity project](https://tc.gortc.io/project.html?projectId=stun&guest=1) and `e2e` directory
for more information. Also the Wireshark `.pcap` files are available for e2e test in
artifacts for build.

# Benchmarks

Intel(R) Core(TM) i7-8700K:

```
version: 1.16.5
goos: linux
goarch: amd64
pkg: github.com/pion/stun
PASS
benchmark                                         iter       time/iter      throughput   bytes alloc        allocs
---------                                         ----       ---------      ----------   -----------        ------
BenchmarkMappedAddress_AddTo-12               30000000     36.40 ns/op                        0 B/op   0 allocs/op
BenchmarkAlternateServer_AddTo-12             50000000     36.70 ns/op                        0 B/op   0 allocs/op
BenchmarkAgent_GC-12                            500000   2552.00 ns/op                        0 B/op   0 allocs/op
BenchmarkAgent_Process-12                     50000000     38.00 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_GetNotFound-12              200000000      6.90 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_Get-12                      200000000      7.61 ns/op                        0 B/op   0 allocs/op
BenchmarkClient_Do-12                          2000000   1072.00 ns/op                        0 B/op   0 allocs/op
BenchmarkErrorCode_AddTo-12                   20000000     67.00 ns/op                        0 B/op   0 allocs/op
BenchmarkErrorCodeAttribute_AddTo-12          30000000     52.20 ns/op                        0 B/op   0 allocs/op
BenchmarkErrorCodeAttribute_GetFrom-12       100000000     12.00 ns/op                        0 B/op   0 allocs/op
BenchmarkFingerprint_AddTo-12                 20000000    102.00 ns/op     430.08 MB/s        0 B/op   0 allocs/op
BenchmarkFingerprint_Check-12                 30000000     54.80 ns/op     948.38 MB/s        0 B/op   0 allocs/op
BenchmarkBuildOverhead/Build-12                5000000    333.00 ns/op                        0 B/op   0 allocs/op
BenchmarkBuildOverhead/BuildNonPointer-12      3000000    536.00 ns/op                      100 B/op   4 allocs/op
BenchmarkBuildOverhead/Raw-12                 10000000    181.00 ns/op                        0 B/op   0 allocs/op
BenchmarkMessageIntegrity_AddTo-12             1000000   1053.00 ns/op      18.98 MB/s        0 B/op   0 allocs/op
BenchmarkMessageIntegrity_Check-12             1000000   1135.00 ns/op      28.17 MB/s        0 B/op   0 allocs/op
BenchmarkMessage_Write-12                    100000000     27.70 ns/op    1011.09 MB/s        0 B/op   0 allocs/op
BenchmarkMessageType_Value-12               2000000000      0.49 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_WriteTo-12                  100000000     12.80 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_ReadFrom-12                  50000000     25.00 ns/op     801.19 MB/s        0 B/op   0 allocs/op
BenchmarkMessage_ReadBytes-12                100000000     18.00 ns/op    1113.03 MB/s        0 B/op   0 allocs/op
BenchmarkIsMessage-12                       2000000000      1.08 ns/op   18535.57 MB/s        0 B/op   0 allocs/op
BenchmarkMessage_NewTransactionID-12           2000000    673.00 ns/op                        0 B/op   0 allocs/op
BenchmarkMessageFull-12                        5000000    316.00 ns/op                        0 B/op   0 allocs/op
BenchmarkMessageFullHardcore-12               20000000     88.90 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_WriteHeader-12              200000000      8.18 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_CloneTo-12                   30000000     37.90 ns/op    1795.32 MB/s        0 B/op   0 allocs/op
BenchmarkMessage_AddTo-12                    300000000      4.77 ns/op                        0 B/op   0 allocs/op
BenchmarkDecode-12                           100000000     22.00 ns/op                        0 B/op   0 allocs/op
BenchmarkUsername_AddTo-12                    50000000     23.20 ns/op                        0 B/op   0 allocs/op
BenchmarkUsername_GetFrom-12                 100000000     17.90 ns/op                        0 B/op   0 allocs/op
BenchmarkNonce_AddTo-12                       50000000     34.40 ns/op                        0 B/op   0 allocs/op
BenchmarkNonce_AddTo_BadLength-12            200000000      8.29 ns/op                        0 B/op   0 allocs/op
BenchmarkNonce_GetFrom-12                    100000000     17.50 ns/op                        0 B/op   0 allocs/op
BenchmarkUnknownAttributes/AddTo-12           30000000     48.10 ns/op                        0 B/op   0 allocs/op
BenchmarkUnknownAttributes/GetFrom-12        100000000     20.90 ns/op                        0 B/op   0 allocs/op
BenchmarkXOR-12                               50000000     25.80 ns/op   39652.86 MB/s        0 B/op   0 allocs/op
BenchmarkXORSafe-12                            3000000    515.00 ns/op    1988.04 MB/s        0 B/op   0 allocs/op
BenchmarkXORFast-12                           20000000     73.40 ns/op   13959.30 MB/s        0 B/op   0 allocs/op
BenchmarkXORMappedAddress_AddTo-12            20000000     56.70 ns/op                        0 B/op   0 allocs/op
BenchmarkXORMappedAddress_GetFrom-12          50000000     37.40 ns/op                        0 B/op   0 allocs/op
ok  	github.com/pion/stun	76.868s
```

### License
MIT License - see [LICENSE](LICENSE) for full text
