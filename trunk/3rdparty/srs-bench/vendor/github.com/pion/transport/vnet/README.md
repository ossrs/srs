# vnet
A virtual network layer for pion.

## Overview

### Goals
* To make NAT traversal tests easy.
* To emulate packet impairment at application level for testing.
* To monitor packets at specified arbitrary interfaces.

### Features
* Configurable virtual LAN and WAN
* Virtually hosted ICE servers

### Virtual network components

#### Top View
```
                           ......................................
                           :         Virtual Network (vnet)     :
                           :                                    :
   +-------+ *         1 +----+         +--------+              :
   | :App  |------------>|:Net|--o<-----|:Router |              :
   +-------+             +----+         |        |              :
   +-----------+ *     1 +----+         |        |              :
   |:STUNServer|-------->|:Net|--o<-----|        |              :
   +-----------+         +----+         |        |              :
   +-----------+ *     1 +----+         |        |              :
   |:TURNServer|-------->|:Net|--o<-----|        |              :
   +-----------+         +----+ [1]     |        |              :
                           :          1 |        | 1  <<has>>   :
                           :      +---<>|        |<>----+ [2]   :
                           :      |     +--------+      |       :
                         To form  |      *|             v 0..1  :
                   a subnet tree  |       o [3]      +-----+    :
                           :      |       ^          |:NAT |    :
                           :      |       |          +-----+    :
                           :      +-------+                     :
                           ......................................
    Note:
        o: NIC (Netork Interface Controller)
      [1]: Net implments NIC interface.
      [2]: Root router has no NAT. All child routers have a NAT always.
      [3]: Router implements NIC interface for accesses from the
           parent router.
```

#### Net
Net provides 3 interfaces:
* Configuration API (direct)
* Network API via Net (equivalent to net.Xxx())
* Router access via NIC interface
```
                   (Pion module/app, ICE servers, etc.)
                             +-----------+
                             |   :App    |
                             +-----------+
                                 * | 
                                   | <<uses>>
                                 1 v
   +---------+ 1           * +-----------+ 1    * +-----------+ 1    * +------+
 ..| :Router |----+------>o--|   :Net    |<>------|:Interface |<>------|:Addr |
   +---------+    |      NIC +-----------+        +-----------+        +------+
                  | <<interface>>               (vnet.Interface)      (net.Addr)
                  |
                  |        * +-----------+ 1    * +-----------+ 1    * +------+
                  +------>o--|  :Router  |<>------|:Interface |<>------|:Addr |
                         NIC +-----------+        +-----------+        +------+
                    <<interface>>               (vnet.Interface)      (net.Addr)
```

> The instance of `Net` will be the one passed around the project.
> Net class has public methods for configuration and for application use.


## Implementation

### Design Policy
* Each pion package should have config object which has `Net` (of type vnet.Net) property. (just like how
        we distribute `LoggerFactory` throughout the pion project.
* DNS => a simple dictionary (global)?
* Each Net has routing capability (a goroutine)
* Use interface provided net package as much as possible
* Routers are connected in a tree structure (no loop is allowed)
   - To simplify routing
   - Easy to control / monitor (stats, etc)
* Root router has no NAT (== Internet / WAN)
* Non-root router has a NAT always
* When a Net is instantiated, it will automatically add `lo0` and `eth0` interface, and `lo0` will
have one IP address, 127.0.0.1. (this is not used in pion/ice, however)
* When a Net is added to a router, the router automatically assign an IP address for `eth0`
interface.
   - For simplicity
* User data won't fragment, but optionally drop chunk larger than MTU
* IPv6 is not supported

### Basic steps for setting up virtual network
1. Create a root router (WAN)
1. Create child routers and add to its parent (forms a tree, don't create a loop!)
1. Add instances of Net to each routers
1. Call Stop(), or Stop(), on the top router, which propages all other routers

#### Example: WAN with one endpoint (vnet)
```go
import (
	"net"

	"github.com/pion/transport/vnet"
	"github.com/pion/logging"
)

// Create WAN (a root router).
wan, err := vnet.NewRouter(&RouterConfig{
    CIDR:          "0.0.0.0/0",
    LoggerFactory: logging.NewDefaultLoggerFactory(),
})

// Create a network.
// You can specify a static IP for the instance of Net to use. If not specified,
// router will assign an IP address that is contained in the router's CIDR.
nw := vnet.NewNet(&vnet.NetConfig{
    StaticIP: "27.1.2.3",
})

// Add the network to the router.
// The router will assign an IP address to `nw`.
if err = wan.AddNet(nw); err != nil {
    // handle error
}

// Start router.
// This will start internal goroutine to route packets.
// If you set child routers (using AddRouter), the call on the root router
// will start the rest of routers for you.
if err = wan.Start(); err != nil {
    // handle error
}

//
// Your application runs here using `nw`.
//

// Stop the router.
// This will stop all internal goroutines in the router tree.
// (No need to call Stop() on child routers)
if err = wan.Stop(); err != nil {
    // handle error
}
```

#### Example of how to pass around the instance of vnet.Net
The instance of vnet.Net wraps a subset of net package to enable operations
on the virtual network. Your project must be able to pass the instance to
all your routines that do network operation with net package. A typical way
is to use a config param to create your instances with the virtual network
instance (`nw` in the above example) like this:

```go
type AgentConfig struct {
    :
    Net:  *vnet.Net,
}

type Agent struct {
     :
    net:  *vnet.Net,
}

func NetAgent(config *AgentConfig) *Agent {
    if config.Net == nil {
        config.Net = vnet.NewNet(nil) // defaults to native operation
    }
    
    return &Agent {
         :
        net: config.Net,
    }
}
```

```go
// a.net is the instance of vnet.Net class
func (a *Agent) listenUDP(...) error {
    conn, err := a.net.ListenPacket(udpString, ...)
    if err != nil {
        return nil, err
    }
      :
}
```


### Compatibility and Support Status

|`net`<br>(built-in)|`vnet`|Note|
|---|---|---|
|net.Interfaces()|a.net.Interfaces()||
|net.InterfaceByName()|a.net.InterfaceByName()||
|net.ResolveUDPAddr()|a.net.ResolveUDPAddr()||
|net.ListenPacket()|a.net.ListenPacket()||
|net.ListenUDP()|a.net.ListenUDP()|(ListenPacket() is recommended)|
|net.Listen()|a.net.Listen()|(TODO)|
|net.ListenTCP()|(not supported)|(Listen() would be recommended)|
|net.Dial()|a.net.Dial()||
|net.DialUDP()|a.net.DialUDP()||
|net.DialTCP()|(not supported)||
|net.Interface|vnet.Interface||
|net.PacketConn|(use it as-is)||
|net.UDPConn|vnet.UDPConn|Use vnet.UDPPacketConn in your code|
|net.TCPConn|vnet.TCPConn|(TODO)|Use net.Conn in your code|
|net.Dialer|vnet.Dialer|Use a.net.CreateDialer() to create it.<br>The use of vnet.Dialer is currently experimental.|

> `a.net` is an instance of Net class, and types are defined under the package name `vnet`

> Most of other `interface` types in net package can be used as is.

> Please post a github issue when other types/methods need to be added to vnet/vnet.Net.

## TODO / Next Step
* Implement TCP (TCPConn, Listen)
* Support of IPv6
* Write a bunch of examples for building virtual networks.
* Add network impairment features (on Router)
  - Introduce lantecy / jitter
  - Packet filtering handler (allow selectively drop packets, etc.)
* Add statistics data retrieval
  - Total number of packets forward by each router
  - Total number of packet loss
  - Total number of connection failure (TCP)

## References
* [Comparing Simulated Packet Loss and RealWorld Network Congestion](https://www.riverbed.com/document/fpo/WhitePaper-Riverbed-SimulatedPacketLoss.pdf)

### Code experiments
* [CIDR and IPMask](https://play.golang.org/p/B7OBhkZqjmj)
* [Test with net.IP](https://play.golang.org/p/AgXd23wKY4W)
* [ListenPacket](https://play.golang.org/p/d4vasbnRimQ)
* [isDottedIP()](https://play.golang.org/p/t4aZ47TgJfO)
* [SplitHostPort](https://play.golang.org/p/JtvurlcMbhn)
