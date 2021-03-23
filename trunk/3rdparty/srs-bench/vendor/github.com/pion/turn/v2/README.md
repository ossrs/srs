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
  <a href="https://travis-ci.org/pion/turn"><img src="https://travis-ci.org/pion/turn.svg?branch=master" alt="Build Status"></a>
  <a href="https://pkg.go.dev/github.com/pion/turn/v2"><img src="https://godoc.org/github.com/pion/turn?status.svg" alt="GoDoc"></a>
  <a href="https://codecov.io/gh/pion/turn"><img src="https://codecov.io/gh/pion/turn/branch/master/graph/badge.svg" alt="Coverage Status"></a>
  <a href="https://goreportcard.com/report/github.com/pion/turn"><img src="https://goreportcard.com/badge/github.com/pion/turn" alt="Go Report Card"></a>
  <a href="https://www.codacy.com/app/pion/turn"><img src="https://api.codacy.com/project/badge/Grade/d53ec6c70576476cb16c140c2964afde" alt="Codacy Badge"></a>
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

# Using
`pion/turn` is an API for building STUN/TURN clients and servers, not a binary you deploy then configure. It may require copying our examples and
making minor modifications to fit your need, no knowledge of Go is required however. You may be able to download the pre-made binaries of our examples
if you wish to get started quickly.

The advantage of this is that you don't need to deal with complicated config files, or custom APIs to modify the state of Pion TURN.
After you instantiate an instance of a Pion TURN server or client you interact with it like any library. The quickest way to get started is to look at the
[examples](examples) or [GoDoc](https://godoc.org/github.com/pion/turn)

# Examples
We try to cover most common use cases in [examples](examples). If more examples could be helpful please file an issue, we are always looking
to expand and improve `pion/turn` to make it easier for developers.

To build any example you just need to run `go build` in the directory of the example you care about.
It is also very easy to [cross compile](https://dave.cheney.net/2015/08/22/cross-compilation-with-go-1-5) Go programs.

You can also see `pion/turn` usage in [pion/ice](https://github.com/pion/ice)

# [FAQ](https://github.com/pion/webrtc/wiki/FAQ)

### RFCs
#### Implemented
* [RFC 5389: Session Traversal Utilities for NAT (STUN)](https://tools.ietf.org/html/rfc5389)
* [RFC 5766: Traversal Using Relays around NAT (TURN)](https://tools.ietf.org/html/rfc5766)

#### Planned
* [RFC 6062: Traversal Using Relays around NAT (TURN) Extensions for TCP Allocations](https://tools.ietf.org/html/rfc6062)
* [RFC 6156: Traversal Using Relays around NAT (TURN) Extension for IPv6](https://tools.ietf.org/html/rfc6156)

### Community
Pion has an active community on the [Golang Slack](https://pion.ly/slack). Sign up and join the **#pion** channel for discussions and support.

We are always looking to support **your projects**. Please reach out if you have something to build!

### Contributing
Check out the [CONTRIBUTING.md](CONTRIBUTING.md) to join the group of amazing people making this project possible:

* [Michiel De Backker](https://github.com/backkem) - *Documentation*
* [Ingmar Wittkau](https://github.com/iwittkau) - *STUN client*
* [John Bradley](https://github.com/kc5nra) - *Original Author*
* [jose nazario](https://github.com/paralax) - *Documentation*
* [Mészáros Mihály](https://github.com/misi) - *Documentation*
* [Mike Santry](https://github.com/santrym) - *Mascot*
* [Sean DuBois](https://github.com/Sean-Der) - *Original Author*
* [winds2016](https://github.com/winds2016) - *Windows platform testing*
* [songjiayang](https://github.com/songjiayang) - *SongJiaYang*
* [Yutaka Takeda](https://github.com/enobufs) - *vnet*
* [namreg](https://github.com/namreg) - *Igor German*
* [Aleksandr Razumov](https://github.com/ernado) - *protocol*
* [Robert Eperjesi](https://github.com/epes)
* [Lukas Rezek](https://github.com/lrezek)
* [Hugo Arregui](https://github.com/hugoArregui)
* [Aaron France](https://github.com/AeroNotix)
* [Atsushi Watanabe](https://github.com/at-wat)
* [Tom Clift](https://github.com/tclift)
* [lllf](https://github.com/LittleLightLittleFire)
* nindolabs (Marouane)
* [Onwuka Gideon](https://github.com/dongido001)
* [Herman Banken](https://github.com/hermanbanken)
* [Jannis Mattheis](https://github.com/jmattheis)

### License
MIT License - see [LICENSE.md](LICENSE.md) for full text


