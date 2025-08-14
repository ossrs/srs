<h1 align="center">
  <br>
  Pion mDNS
  <br>
</h1>
<h4 align="center">A Go implementation of mDNS</h4>
<p align="center">
  <a href="https://pion.ly"><img src="https://img.shields.io/badge/pion-mdns-gray.svg?longCache=true&colorB=brightgreen" alt="Pion mDNS"></a>
  <a href="https://pion.ly/slack"><img src="https://img.shields.io/badge/join-us%20on%20slack-gray.svg?longCache=true&logo=slack&colorB=brightgreen" alt="Slack Widget"></a>
  <br>
  <img alt="GitHub Workflow Status" src="https://img.shields.io/github/actions/workflow/status/pion/mdns/test.yaml">
  <a href="https://pkg.go.dev/github.com/pion/mdns"><img src="https://pkg.go.dev/badge/github.com/pion/mdsn.svg" alt="Go Reference"></a>
  <a href="https://codecov.io/gh/pion/mdns"><img src="https://codecov.io/gh/pion/mdns/branch/master/graph/badge.svg" alt="Coverage Status"></a>
  <a href="https://goreportcard.com/report/github.com/pion/mdns"><img src="https://goreportcard.com/badge/github.com/pion/mdns" alt="Go Report Card"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>
<br>

Go mDNS implementation. The original user is Pion WebRTC, but we would love to see it work for everyone.

### Running Server
For a mDNS server that responds to queries for `pion-test.local`
```sh
go run examples/server/main.go
```

For a mDNS server that responds to queries for `pion-test.local` with a given address
```sh
go run examples/server/publish_ip/main.go -ip=[IP]
```
If you don't set the `ip` parameter, "1.2.3.4" will be used instead.


### Running Client
To query using Pion you can run the `query` example
```sh
go run examples/query/main.go
```

You can use the macOS client
```
dns-sd -q pion-test.local
```

Or the avahi client
```
avahi-resolve -a pion-test.local
```

### RFCs
#### Implemented
- **RFC 6762** [Multicast DNS][rfc6762]
- **draft-ietf-rtcweb-mdns-ice-candidates-02** [Using Multicast DNS to protect privacy when exposing ICE candidates](https://datatracker.ietf.org/doc/html/draft-ietf-rtcweb-mdns-ice-candidates-02.html)

[rfc6762]: https://tools.ietf.org/html/rfc6762

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