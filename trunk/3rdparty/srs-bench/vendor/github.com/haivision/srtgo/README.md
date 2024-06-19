[![PkgGoDev](https://pkg.go.dev/badge/github.com/haivision/srtgo)](https://pkg.go.dev/github.com/haivision/srtgo)

# srtgo

Go bindings for [SRT](https://github.com/Haivision/srt) (Secure Reliable Transport), the open source transport technology that optimizes streaming performance across unpredictable networks.

## Why srtgo?
The purpose of srtgo is easing the adoption of SRT transport technology. Using Go, with just a few lines of code you can implement an application that sends/receives data with all the benefits of SRT technology: security and reliability, while keeping latency low.

## Is this a new implementation of SRT?
No! We are just exposing the great work done by the community in the [SRT project](https://github.com/Haivision/srt) as a golang library. All the functionality and implementation still resides in the official SRT project.


# Features supported
* Basic API exposed to easy develop SRT sender/receiver apps
* Caller and Listener mode
* Live transport type
* File transport type
* Message/Buffer API
* SRT transport options up to SRT 1.4.1
* SRT Stats retrieval

# Usage
Example of a SRT receiver application:
``` go
package main

import (
    "github.com/haivision/srtgo"
    "fmt"
)

func main() {
    options := make(map[string]string)
    options["transtype"] = "file"

    sck := srtgo.NewSrtSocket("0.0.0.0", 8090, options)
    defer sck.Close()
    sck.Listen(1)
    s, _ := sck.Accept()
    defer s.Close()

    buf := make([]byte, 2048)
    for {
        n, _ := s.Read(buf)
        if n == 0 {
            break
        }
        fmt.Println("Received %d bytes", n)
    }
    //....
}

```


# Dependencies

* srtlib

You can find detailed instructions about how to install srtlib in its [README file](https://github.com/Haivision/srt#requirements)

gosrt has been developed with srt 1.4.1 as its main target and has been successfully tested in srt 1.3.4 and above.
