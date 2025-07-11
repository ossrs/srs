<h1 align="center">
  Design
</h1>
WebRTC is a powerful, but complicated technology you can build amazing things with, it comes with a steep learning curve though.
Using WebRTC in the browser is easy, but outside the browser is more of a challenge. There are multiple libraries, and they all have
varying levels of quality. Most are also difficult to build, and depend on libraries that aren't available in repos or portable.

Pion WebRTC aims to solve all that! Built in native Go you should be able to send and receive media and text from anywhere with minimal headache.
These are the design principals that drive Pion WebRTC and hopefully convince you it is worth a try.

### Portable
Pion WebRTC is written in Go and extremely portable. Anywhere Golang runs, Pion WebRTC should work as well! Instead of dealing with complicated
cross-compiling of multiple libraries, you now can run anywhere with one `go build`

### Flexible
When possible we leave all decisions to the user. When choice is possible (like what logging library is used) we defer to the developer.

### Simple API
If you know how to use WebRTC in your browser, you know how to use Pion WebRTC.
We try our best just to duplicate the Javascript API, so your code can look the same everywhere.

If this is your first time using WebRTC, don't worry! We have multiple [examples](https://github.com/pion/webrtc/tree/master/examples) and [GoDoc](https://pkg.go.dev/github.com/pion/webrtc/v4)

### Bring your own media
Pion WebRTC doesn't make any assumptions about where your audio, video or text come from. You can use FFmpeg, GStreamer, MLT or just serve a video file.
This library only serves to transport, not create media.

### Safe
Golang provides a great foundation to build safe network services.
Especially when running a networked service that is highly concurrent bugs can be devastating.

### Readable
If code comes from an RFC we try to make sure everything is commented with a link to the spec.
This makes learning and debugging easier, this WebRTC library was written to also serve as a guide for others.

### Tested
Every commit is tested via travis-ci Go provides fantastic facilities for testing, and more will be added as time goes on.

### Shared libraries
Every Pion project is built using shared libraries, allowing others to review and reuse our libraries.

### Community
The most important part of Pion is the community. This projects only exist because of individual contributions. We aim to be radically open and do everything we can to support those that make Pion possible.
