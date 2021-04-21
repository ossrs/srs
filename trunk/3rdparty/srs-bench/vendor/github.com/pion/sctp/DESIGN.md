<h1 align="center">
  Design
</h1>

### Portable
Pion SCTP is written in Go and extremely portable. Anywhere Golang runs, Pion SCTP should work as well! Instead of dealing with complicated
cross-compiling of multiple libraries, you now can run anywhere with one `go build`

### Simple API
The API is based on an io.ReadWriteCloser.

### Readable
If code comes from an RFC we try to make sure everything is commented with a link to the spec.
This makes learning and debugging easier, this library was written to also serve as a guide for others.

### Tested
Every commit is tested via travis-ci Go provides fantastic facilities for testing, and more will be added as time goes on.

### Shared libraries
Every pion product is built using shared libraries, allowing others to review and reuse our libraries.
