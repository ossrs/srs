## Introduction
In response to the modifications made to the permissions for accessing system MAC addresses in Android 11, ordinary applications encounter several main issues when using NETLINK sockets:

- Not allowing bind operations on `NETLINK` sockets.
- Not permitting the use of the `RTM_GETLINK` functionality.

For detailed information, please refer to: https://developer.android.com/training/articles/user-data-ids#mac-11-plus

As a result of the aforementioned reasons, using `net.Interfaces()` and `net.InterfaceAddrs()` from the Go net package in the Android environment leads to the `route ip+net: netlinkrib: permission denied` error. 

You can find specific issue details here: https://github.com/golang/go/issues/40569

To address the issue of using the Go net package in the Android environment, we have made partial modifications to its source code to ensure proper functionality on Android. 

I have fully resolved the issues with `net.InterfaceAddrs()`. 

However, for `net.Interfaces()`, we have only addressed some problems, as the following issues still remain:
- It can only return interfaces with IP addresses.
- It cannot return hardware MAC addresses.

Nevertheless, the fixed `net.Interfaces()` function now aligns with the Android API's `NetworkInterface.getNetworkInterfaces()` and can be used normally in most scenarios.

The specific fix logic includes:

Removing the `Bind()` operation on `Netlink` sockets in the `NetlinkRIB()` function.
Using `ioctl` based on the Index number returned by `RTM_GETADDR` to retrieve the network card's name, MTU, and flags.

There are two implementations of the `net` package: one from the [Go standard library](https://pkg.go.dev/net) and another from the [golang.org/x/net](https://pkg.go.dev/golang.org/x/net) module. Both of these implementations have the same issues in the Android environment. The `anet` package should be compatible with both of them.

## Test Code
### net.Interface()
use `net.Interface()`:
```go
func RawInterface() {
	interfaces, err := net.Interfaces()
	if err != nil {
		panic(err)
	}

	for _, i := range interfaces {
		log.Println(i)
	}
}
```
result:
```
panic: route ip+net: netlinkrib: permission denied
```

use `anet.Interface()`:
```go
func AnetInterface() {
	interfaces, err := anet.Interfaces()
	if err != nil {
		panic(err)
	}

	for _, i := range interfaces {
		log.Println(i)
	}
}
```

result:
```
{1 65536 lo  up|loopback|running}
{15 1400 rmnet_data1  up|running}
{24 1500 wlan0  up|broadcast|multicast|running}
{3 1500 dummy0  up|broadcast|running}
{4 1500 ifb0  up|broadcast|running}
{5 1500 ifb1  up|broadcast|running}
{12 1500 ifb2  up|broadcast|running}
{14 1500 rmnet_data0  up|running}
{16 1400 rmnet_data2  up|running}
{17 1400 rmnet_data3  up|running}
```

### net.InterfaceAddrs()
use `net.InterfaceAddrs()`:
```go
func NetInterfaceAddrs() {
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		panic(err)
	}

	for _, addr := range addrs {
		log.Println(addr)
	}
}
```
result: 
```
panic: route ip+net: netlinkrib: permission denied
```

use `anet.InterfaceAddrs()`:
```go
func AnetInterfaceAddrs() {
	addrs, err := anet.InterfaceAddrs()
	if err != nil {
		panic(err)
	}

	for _, addr := range addrs {
		log.Println(addr)
	}
}
```
result:
```
127.0.0.1/8
::1/128
...
192.168.6.143/24
fe80::7e4f:4446:eb3:1eb8/64
```

## Other issues due to #40569
- https://github.com/golang/go/issues/68082

## How to build with Go 1.23.0 or later
The `anet` library internally relies on `//go:linkname` directive. Since the usage of `//go:linkname` has been restricted since Go 1.23.0 ([Go 1.23 Release Notes](https://tip.golang.org/doc/go1.23#linker)), it is necessary to specify the `-checklinkname=0` linker flag when building the `anet` package with Go 1.23.0 or later. Without this flag, the following linker error will occur:
```
link: github.com/wlynxg/anet: invalid reference to net.zoneCache
```
