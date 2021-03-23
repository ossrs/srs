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
  <a href="https://travis-ci.org/pion/mdns"><img src="https://travis-ci.org/pion/mdns.svg?branch=master" alt="Build Status"></a>
  <a href="https://godoc.org/github.com/pion/mdns"><img src="https://godoc.org/github.com/pion/mdns?status.svg" alt="GoDoc"></a>
  <a href="https://codecov.io/gh/pion/mdns"><img src="https://codecov.io/gh/pion/mdns/branch/master/graph/badge.svg" alt="Coverage Status"></a>
  <a href="https://goreportcard.com/report/github.com/pion/mdns"><img src="https://goreportcard.com/badge/github.com/pion/mdns" alt="Go Report Card"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>
<br>

Go mDNS implementation. The original user is Pion WebRTC, but we would love to see it work for everyone.

### Running Server
For a mDNS server that responds to queries for `pion-test.local`
```sh
go run examples/listen/main.go
```


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

### References
https://tools.ietf.org/html/rfc6762
https://tools.ietf.org/id/draft-ietf-rtcweb-mdns-ice-candidates-02.html

### Community
Pion has an active community on the [Golang Slack](https://invite.slack.golangbridge.org/). Sign up and join the **#pion** channel for discussions and support. You can also use [Pion mailing list](https://groups.google.com/forum/#!forum/pion).

We are always looking to support **your projects**. Please reach out if you have something to build!

If you need commercial support or don't want to use public methods you can contact us at [team@pion.ly](mailto:team@pion.ly)

### Contributing
Check out the **[contributing wiki](https://github.com/pion/webrtc/wiki/Contributing)** to join the group of amazing people making this project possible:

* [Sean DuBois](https://github.com/Sean-Der) - *Original Author*
* [Konstantin Itskov](https://github.com/trivigy) - Contributor
* [Hugo Arregui](https://github.com/hugoArregui)

### License
MIT License - see [LICENSE](LICENSE) for full text
