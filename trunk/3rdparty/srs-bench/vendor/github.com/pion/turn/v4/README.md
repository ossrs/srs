<h1 align="center">
  <a href="https://pion.ly"><img src="./.github/gopher-pion.png" alt="Pion TURN" height="250px"></a>
  <br>
  Pion TURN
  <br>
</h1>
<h4 align="center">A toolkit for building TURN clients and servers in Go</h4>
<p align="center">
  <a href="https://pion.ly"><img src="https://img.shields.io/badge/pion-turn-gray.svg?longCache=true&colorB=brightgreen" alt="Pion TURN"></a>
  <a href="http://gophers.slack.com/messages/pion"><img src="https://img.shields.io/badge/join-us%20on%20slack-gray.svg?longCache=true&logo=slack&colorB=brightgreen" alt="Slack Widget"></a>
  <a href="https://github.com/pion/awesome-pion" alt="Awesome Pion"><img src="https://cdn.rawgit.com/sindresorhus/awesome/d7305f38d29fed78fa85652e3a63e154dd8e8829/media/badge.svg"></a>
  <br>
  <img alt="GitHub Workflow Status" src="https://img.shields.io/github/actions/workflow/status/pion/turn/test.yaml">
  <a href="https://pkg.go.dev/github.com/pion/turn/v3"><img src="https://pkg.go.dev/badge/github.com/pion/turn/v3.svg" alt="Go Reference"></a>
  <a href="https://codecov.io/gh/pion/turn"><img src="https://codecov.io/gh/pion/turn/branch/master/graph/badge.svg" alt="Coverage Status"></a>
  <a href="https://goreportcard.com/report/github.com/pion/turn/v3"><img src="https://goreportcard.com/badge/github.com/pion/turn/v3" alt="Go Report Card"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>
<br>

Pion TURN is a Go toolkit for building TURN servers and clients. We wrote it to solve problems we had when building RTC projects.

* **Deployable** - Use modern tooling of the Go ecosystem. Stop generating config files.
* **Embeddable** - Include `pion/turn` in your existing applications. No need to manage another service.
* **Extendable** - TURN as an API so you can easily integrate with your existing monitoring and metrics.
* **Maintainable** - `pion/turn` is simple and well documented. Designed for learning and easy debugging.
* **Portable** - Quickly deploy to multiple architectures/platforms just by setting an environment variable.
* **Safe** - Stability and safety is important for network services. Go provides everything we need.
* **Scalable** - Create allocations and mutate state at runtime. Designed to make scaling easy.

### Using
`pion/turn` is an API for building STUN/TURN clients and servers, not a binary you deploy then configure. It may require copying our examples and
making minor modifications to fit your need, no knowledge of Go is required however. You may be able to download the pre-made binaries of our examples
if you wish to get started quickly.

The advantage of this is that you don't need to deal with complicated config files, or custom APIs to modify the state of Pion TURN.
After you instantiate an instance of a Pion TURN server or client you interact with it like any library. The quickest way to get started is to look at the
[examples](examples) or [GoDoc](https://godoc.org/github.com/pion/turn)

### Examples
We try to cover most common use cases in [examples](examples). If more examples could be helpful please file an issue, we are always looking
to expand and improve `pion/turn` to make it easier for developers.

To build any example you just need to run `go build` in the directory of the example you care about.
It is also very easy to [cross compile](https://dave.cheney.net/2015/08/22/cross-compilation-with-go-1-5) Go programs.

You can also see `pion/turn` usage in [pion/ice](https://github.com/pion/ice)

### FAQ

Also take a look at the [Pion WebRTC FAQ](https://github.com/pion/webrtc/wiki/FAQ)

#### Will pion/turn also act as a STUN server?
Yes.

#### How do I implement token-based authentication?
Replace the username with a token in the [AuthHandler](https://github.com/pion/turn/blob/6d0ff435910870eb9024b18321b93b61844fcfec/examples/turn-server/simple/main.go#L49).
The password sent by the client can be any non-empty string, as long as it matches that used by the [GenerateAuthKey](https://github.com/pion/turn/blob/6d0ff435910870eb9024b18321b93b61844fcfec/examples/turn-server/simple/main.go#L41)
function.

#### Will WebRTC prioritize using STUN over TURN?
Yes.

### RFCs
#### Implemented
* **RFC 5389**: [Session Traversal Utilities for NAT (STUN)][rfc5389]
* **RFC 5766**: [Traversal Using Relays around NAT (TURN): Relay Extensions to Session Traversal Utilities for NAT (STUN)][rfc5766]

#### Planned
* **RFC 6062**: [Traversal Using Relays around NAT (TURN) Extensions for TCP Allocations][rfc6062]
* **RFC 6156**: [Traversal Using Relays around NAT (TURN) Extension for IPv6][rfc6156]

[rfc5389]: https://tools.ietf.org/html/rfc5389
[rfc5766]: https://tools.ietf.org/html/rfc5766
[rfc6062]: https://tools.ietf.org/html/rfc6062
[rfc6156]: https://tools.ietf.org/html/rfc6156

### Roadmap
The library is used as a part of our WebRTC implementation. Please refer to that [roadmap](https://github.com/pion/webrtc/issues/9) to track our major milestones.

### Community
Pion has an active community on the [Slack](https://pion.ly/slack).

Follow the [Pion Twitter](https://twitter.com/_pion) for project updates and important WebRTC news.

We are always looking to support **your projects**. Please reach out if you have something to build!
If you need commercial support or don't want to use public methods you can contact us at [team@pion.ly](mailto:team@pion.ly)

### Contributing
Check out the [contributing wiki](https://github.com/pion/webrtc/wiki/Contributing) to join the group of amazing people making this project possible

### License
MIT License - see [LICENSE](LICENSE) for full text
