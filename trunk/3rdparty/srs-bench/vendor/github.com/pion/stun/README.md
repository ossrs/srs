<h1 align="center">
  <br>
  Pion STUN
  <br>
</h1>
<h4 align="center">A Go implementation of STUN</h4>
<p align="center">
  <a href="https://pion.ly"><img src="https://img.shields.io/badge/pion-stun-gray.svg?longCache=true&colorB=brightgreen" alt="Pion stun"></a>
  <a href="https://pion.ly/slack"><img src="https://img.shields.io/badge/join-us%20on%20slack-gray.svg?longCache=true&logo=slack&colorB=brightgreen" alt="Slack Widget"></a>
  <br>
  <img alt="GitHub Workflow Status" src="https://img.shields.io/github/actions/workflow/status/pion/stun/test.yaml">
  <a href="https://pkg.go.dev/github.com/pion/stun"><img src="https://pkg.go.dev/badge/github.com/pion/stun.svg" alt="Go Reference"></a>
  <a href="https://codecov.io/gh/pion/stun"><img src="https://codecov.io/gh/pion/stun/branch/master/graph/badge.svg" alt="Coverage Status"></a>
  <a href="https://goreportcard.com/report/github.com/pion/stun"><img src="https://goreportcard.com/badge/github.com/pion/stun" alt="Go Report Card"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>
<br>

Package `stun` implements Session Traversal Utilities for NAT (STUN) ([RFC 5389][rfc5389])
protocol and [client](https://pkg.go.dev/github.com/pion/stun#Client) with no external dependencies and zero allocations in hot paths.
Client [supports](https://pkg.go.dev/github.com/pion/stun#WithRTO) automatic request retransmissions.

### Example
You can get your current IP address from any STUN server by sending
binding request. See more idiomatic example at `cmd/stun-client`.
```go
package main

import (
	"fmt"

	"github.com/pion/stun"
)

func main() {
	// Parse a STUN URI
	u, err := stun.ParseURI("stun:stun.l.google.com:19302")
	if err != nil {
		panic(err)
	}

	// Creating a "connection" to STUN server.
	c, err := stun.DialURI(u, &stun.DialConfig{})
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

### RFCs
#### Implemented
- **RFC 5389**: [Session Traversal Utilities for NAT (STUN)][rfc5389]
- **RFC 5769**: [Test Vectors for Session Traversal Utilities for NAT (STUN)][rfc5769]
- **RFC 6062**: [Traversal Using Relays around NAT (TURN) Extensions for TCP Allocations][rfc6062]
- **RFC 7064**: [URI Scheme for the Session Traversal Utilities for NAT (STUN) Protocol][rfc7064]
- **RFC 7065**: [Traversal Using Relays around NAT (TURN) Uniform Resource Identifiers][rfc7065]
- **RFC 5780**: [NAT Behavior Discovery Using Session Traversal Utilities for NAT (STUN)][rfc5780] via [cmd/stun-nat-behaviour](cmd/stun-nat-behaviour)
- (TLS-over-)TCP client support

#### Planned
- **RFC 5389**: [ALTERNATE-SERVER](https://tools.ietf.org/html/rfc5389#section-11) support [#48](https://github.com/pion/stun/issues/48)

#### Compatability notes

[RFC 5389][rfc5389] obsoletes [RFC 3489][rfc3489], so implementation was ignored by purpose, however,
[RFC 3489][rfc3489] can be easily implemented as separate package.

[rfc3489]: https://tools.ietf.org/html/rfc3489
[rfc5389]: https://tools.ietf.org/html/rfc5389
[rfc5769]: https://tools.ietf.org/html/rfc5769
[rfc5780]: https://tools.ietf.org/html/rfc5780
[rfc6062]: https://tools.ietf.org/html/rfc6062
[rfc7064]: https://tools.ietf.org/html/rfc7064
[rfc7065]: https://tools.ietf.org/html/rfc7065

### Stability
Package is currently stable, no backward incompatible changes are expected
with exception of critical bugs or security fixes.

Additional attributes are unlikely to be implemented in scope of stun package,
the only exception is constants for attribute or message types.

### Requirements
Go 1.12 is currently supported and tested in CI.

### Testing
Client behavior is tested and verified in many ways:
  * End-To-End with long-term credentials
    * **coturn**: The coturn [server](https://github.com/coturn/coturn/wiki/turnserver) (linux)
  * Bunch of code static checkers (linters)
  * Standard unit-tests with coverage reporting (linux {amd64, **arm**64}, windows and darwin)
  * Explicit API backward compatibility [check](https://github.com/gortc/api), see `api` directory

See [TeamCity project](https://tc.gortc.io/project.html?projectId=stun&guest=1) and `e2e` directory
for more information. Also the Wireshark `.pcap` files are available for e2e test in
artifacts for build.

### Benchmarks
Intel(R) Core(TM) i7-8700K:

```
version: 1.22.2
goos: linux
goarch: amd64
pkg: github.com/pion/stun
PASS
benchmark                                         iter       time/iter      throughput   bytes alloc        allocs
---------                                         ----       ---------      ----------   -----------        ------
BenchmarkMappedAddress_AddTo-12               32489450     38.30 ns/op                        0 B/op   0 allocs/op
BenchmarkAlternateServer_AddTo-12             31230991     39.00 ns/op                        0 B/op   0 allocs/op
BenchmarkAgent_GC-12                            431390   2918.00 ns/op                        0 B/op   0 allocs/op
BenchmarkAgent_Process-12                     35901940     36.20 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_GetNotFound-12              242004358      5.19 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_Get-12                      230520343      5.21 ns/op                        0 B/op   0 allocs/op
BenchmarkClient_Do-12                          1282231    943.00 ns/op                        0 B/op   0 allocs/op
BenchmarkErrorCode_AddTo-12                   16318916     75.50 ns/op                        0 B/op   0 allocs/op
BenchmarkErrorCodeAttribute_AddTo-12          21584140     54.80 ns/op                        0 B/op   0 allocs/op
BenchmarkErrorCodeAttribute_GetFrom-12       100000000     11.10 ns/op                        0 B/op   0 allocs/op
BenchmarkFingerprint_AddTo-12                 19368768     64.00 ns/op     687.81 MB/s        0 B/op   0 allocs/op
BenchmarkFingerprint_Check-12                 24167007     49.10 ns/op    1057.99 MB/s        0 B/op   0 allocs/op
BenchmarkBuildOverhead/Build-12                5486252    224.00 ns/op                        0 B/op   0 allocs/op
BenchmarkBuildOverhead/BuildNonPointer-12      2496544    517.00 ns/op                      100 B/op   4 allocs/op
BenchmarkBuildOverhead/Raw-12                  6652118    181.00 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_ForEach-12                   28254212     35.90 ns/op                        0 B/op   0 allocs/op
BenchmarkMessageIntegrity_AddTo-12             1000000   1179.00 ns/op      16.96 MB/s        0 B/op   0 allocs/op
BenchmarkMessageIntegrity_Check-12              975954   1219.00 ns/op      26.24 MB/s        0 B/op   0 allocs/op
BenchmarkMessage_Write-12                     41040598     30.40 ns/op     922.13 MB/s        0 B/op   0 allocs/op
BenchmarkMessageType_Value-12               1000000000      0.53 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_WriteTo-12                   94942935     11.30 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_ReadFrom-12                  43437718     29.30 ns/op     682.87 MB/s        0 B/op   0 allocs/op
BenchmarkMessage_ReadBytes-12                 74693397     15.90 ns/op    1257.42 MB/s        0 B/op   0 allocs/op
BenchmarkIsMessage-12                       1000000000      1.20 ns/op   16653.64 MB/s        0 B/op   0 allocs/op
BenchmarkMessage_NewTransactionID-12            521121   2450.00 ns/op                        0 B/op   0 allocs/op
BenchmarkMessageFull-12                        5389495    221.00 ns/op                        0 B/op   0 allocs/op
BenchmarkMessageFullHardcore-12               12715876     94.40 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_WriteHeader-12              100000000     11.60 ns/op                        0 B/op   0 allocs/op
BenchmarkMessage_CloneTo-12                   30199020     41.80 ns/op    1626.66 MB/s        0 B/op   0 allocs/op
BenchmarkMessage_AddTo-12                    415257625      2.97 ns/op                        0 B/op   0 allocs/op
BenchmarkDecode-12                            49573747     23.60 ns/op                        0 B/op   0 allocs/op
BenchmarkUsername_AddTo-12                    56282674     22.50 ns/op                        0 B/op   0 allocs/op
BenchmarkUsername_GetFrom-12                 100000000     10.10 ns/op                        0 B/op   0 allocs/op
BenchmarkNonce_AddTo-12                       39419097     35.80 ns/op                        0 B/op   0 allocs/op
BenchmarkNonce_AddTo_BadLength-12            196291666      6.04 ns/op                        0 B/op   0 allocs/op
BenchmarkNonce_GetFrom-12                    120857732      9.93 ns/op                        0 B/op   0 allocs/op
BenchmarkUnknownAttributes/AddTo-12           28881430     37.20 ns/op                        0 B/op   0 allocs/op
BenchmarkUnknownAttributes/GetFrom-12         64907534     19.80 ns/op                        0 B/op   0 allocs/op
BenchmarkXOR-12                               32868506     32.20 ns/op   31836.66 MB/s
BenchmarkXORSafe-12                            5185776    234.00 ns/op    4378.74 MB/s
BenchmarkXORFast-12                           30975679     32.50 ns/op   31525.28 MB/s
BenchmarkXORMappedAddress_AddTo-12            21518028     54.50 ns/op                        0 B/op   0 allocs/op
BenchmarkXORMappedAddress_GetFrom-12          35597667     34.40 ns/op                        0 B/op   0 allocs/op
ok      github.com/pion/stun   60.973s
```

### Roadmap
The library is used as a part of our WebRTC implementation. Please refer to that [roadmap](https://github.com/pion/webrtc/issues/9) to track our major milestones.

### Community
Pion has an active community on the [Slack](https://pion.ly/slack).

Follow the [Pion Twitter](https://twitter.com/_pion) for project updates and important WebRTC news.

We are always looking to support **your projects**. Please reach out if you have something to build!
If you need commercial support or don't want to use public methods you can contact us at [team@pion.ly](mailto:team@pion.ly)

### Contributing
Check out the [contributing wiki](https://github.com/pion/webrtc/wiki/Contributing) to join the group of amazing people making this project possible: [AUTHORS.txt](./AUTHORS.txt)

### License
MIT License - see [LICENSE](LICENSE) for full text
