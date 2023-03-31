# SCTP user-land implementation (usrsctp)

SCTP is a message oriented, reliable transport protocol with direct support for multihoming that runs on top of IP or UDP, and supports both v4 and v6 versions.

Like TCP, SCTP provides reliable, connection oriented data delivery with congestion control. Unlike TCP, SCTP also provides message boundary preservation, ordered and unordered message delivery, multi-streaming and multi-homing. Detection of data corruption, loss of data and duplication of data is achieved by using checksums and sequence numbers. A selective retransmission mechanism is applied to correct loss or corruption of data.

In this manual the socket API for the SCTP User-land implementation will be described.  It is based on [RFC 6458](https://tools.ietf.org/html/rfc6458). The main focus of this document is on pointing out the differences to the SCTP Sockets API. For all aspects of the sockets API that are not mentioned in this document, please refer to [RFC 6458](https://tools.ietf.org/html/rfc6458). Questions about SCTP itself can hopefully be answered by [RFC 4960](https://tools.ietf.org/html/rfc4960).

## Getting Started
The user-land stack has been tested on FreeBSD 10.0, OpenBSD 7.0, Ubuntu 11.10, Windows 7, Mac OS X 10.6, and Mac OS X 10.7. The current version of the user-land stack is provided on [github](https://github.com/sctplab/usrsctp). Download the tarball and untar it in a folder of your choice. The tarball contains all the sources to build the libusrsctp, which has to be linked to the object file of an example program. In addition there are two applications in the folder `programs` that can be built and run.

### Building the Library and the Applications
#### Unix-like Operating Systems
In the folder `usrsctp` type

    $ ./bootstrap
    $ ./configure
    $ make

Now, the library `libusrsctp.la` has been built in the subdirectory `usrsctplib`, and the example programs are ready to run from the subdirectory `programs`.

If you have root privileges or are in the sudoer group, you can install the library in `/usr/local/lib` and copy the header file to `/usr/include` with the command

    $ sudo make install

#### Windows
On Windows you need a compiler like Microsoft Visual Studio. You can build the library and the example programs with the command line tool of the compiler by typing

    $ nmake -f Makefile.nmake

in the directory `usrsctp`.

#### CMake
Create a directory outside the `usrsctp` directory, enter it and generate files by typing

    $ cmake <path-to-usrsctp-sources>
    $ cmake --build .

By default CMake generates a DEBUG build with verbose output.

### Running the Test Programs

Several test programs are included, including a discard server and a client. You can run both to send data from the client to the server. The client reads data from stdin and sends them to the server, which prints the message in the terminal and discards it. The sources of the server are also provided [here](https://github.com/sctplab/usrsctp/blob/master/programs/discard_server.c) and those of the client [here](https://github.com/sctplab/usrsctp/blob/master/programs/client.c).

### Using UDP Encapsulation

Both programs can either send data over SCTP directly or use UDP encapsulation, thus encapsulating the SCTP packet in a UDP datagram. The first mode works on loopback or in a protected setup without any NAT boxes involved. In all other cases it is better to use UDP encapsulation.

The usage of the `discard_server` is

    $ discard_server [local_encaps_port remote_encaps_port]

For UDP encapsulation the ports have to be specified. The local and remote encapsulation ports can be arbitrarily set. For example, you can call

    $ ./discard_server 11111 22222

on a Unix-like OS and

    $ discard_server.exe 11111 22222

on Windows.

The client needs two additional parameters, the server's address and its port. Its usage is

    $ client remote_addr remote_port [local_port local_encaps_port remote_encaps_port]

The remote address is the server's address. If client and server are started on the same machine, the loopback address `127.0.0.1` can be used for Unix-like OSs and the local address on Windows. The discard port is 9, thus 9 has to be taken as remote port. The encapsulation ports have to match those of the server, i.e. the server's `local_encaps_port` is the client's `remote_encaps_port` and vice versa. Thus, the client can be started with

    $ ./client 127.0.0.1 9 0 22222 11111

on a Unix-like OS and

    $ client.exe 192.168.0.1 9 0 22222 11111

on Windows provided your local IP address is 192.168.0.1.

### Sending over SCTP

To send data over SCTP directly you might need root privileges because raw sockets are used. Thus instead of specifying the encapsulation ports you have to start the programs prepending `sudo` or in case of Windows start the program from an administrator console.

### Using the Callback API

Instead of asking constantly for new data, a callback API can be used that is triggered by SCTP. A callback function has to be registered that will be called whenever data is ready to be delivered to the application.

The `discard_server` has a flag to switch between the two modi. If  `use_cb` is set to 1, the callback API will be used. To change the setting, just set the flag and compile the program again.


## Basic Operations

All system calls start with the prefix `usrsctp_` to distinguish them from the kernel variants. Some of them are changed to account for the different demands in the userland environment.

## Differences to RFC 6458

### usrsctp_init()

Every application has to start with `usrsctp_init()`. This function calls `sctp_init()` and reserves the memory necessary to administer the data transfer. The function prototype is

```c
void usrsctp_init(uint16_t udp_port)
```

As it is not always possible to send data directly over SCTP because not all NAT boxes can process SCTP packets, the data can be sent over UDP. To encapsulate SCTP into UDP a UDP port has to be specified, to which the datagrams can be sent. This local UDP port  is set with the parameter `udp_port`. The default value is 9899, the standard UDP encapsulation port. If UDP encapsulation is not necessary, the UDP port has to be set to 0.

### usrsctp_finish()

At the end of the program `usrsctp_finish()` should be called to free all the memory that has been allocated before. The function prototype is

```c
int usrsctp_finish(void)
```

The return code is 0 on success and -1 in case of an error.

### usrsctp_socket()

A representation of an SCTP endpoint is a socket. Is it created with `usrsctp_socket()`. The function prototype is:

```c
struct socket *
usrsctp_socket(int domain,
               int type,
               int protocol,
               int (*receive_cb)(struct socket *sock,
                                 union sctp_sockstore addr,
                                 void *data,
                                 size_t datalen,
                                 struct sctp_rcvinfo,
                                 int flags,
                                 void *ulp_info),
               int (*send_cb)(struct socket *sock,
                              uint32_t sb_free,
                              void *ulp_info),
               uint32_t sb_threshold,
               void *ulp_info)
```

The arguments taken from [RFC 6458](https://tools.ietf.org/html/rfc6458) are:

* domain: PF_INET or PF_INET6 can be used.
* type: In case of a one-to-many style socket it is SOCK_SEQPACKET, in case of a one-to-one style
socket it is SOCK_STREAM. For an explanation of the differences between the socket types please
refer to [RFC 6458](https://tools.ietf.org/html/rfc6458).
* protocol: Set IPPROTO_SCTP.

In usrsctp, a callback API can be used.

* The function pointers of the receive and send callbacks are new arguments to the socket call. If no callback API is used, these must be `NULL`.
* The `sb_threshold` specifies the amount of free space in the send socket buffer before the send function in the application is called. If a send callback function is specified and `sb_threshold` is 0, the function is called whenever there is room in the send socket buffer.
* Additional data may be passed along within the `ulp_info` parameter. This value will be passed to the `receive_cb` when it is invoked.

On success `usrsctp_socket()` returns the pointer to the new socket in the `struct socket` data type. It will be needed in all other system calls. In case of a failure NULL is returned and errno is set to the appropriate error code.

### usrsctp_close()

The function prototype of `usrsctp_close()` is

```c
void usrsctp_close(struct socket *so)
 ```
Thus the only difference is the absence of a return code.

## Same Functionality as RFC 6458

The following functions have the same functionality as their kernel pendants. There prototypes
are described in the following subsections. For a detailed description please refer to [RFC 6458](https://tools.ietf.org/html/rfc6458).

### usrsctp_bind()

```c
int
usrsctp_bind(struct socket *so,
             struct sockaddr *addr,
             socklen_t addrlen)
```

* so: Pointer to the socket as returned by `usrsctp_socket()`.
* addr: The address structure (`struct sockaddr_in` for an IPv4 address or `struct sockaddr_in6` for an IPv6 address).
* addrlen: The size of the address structure.

`usrsctp_bind()` returns 0 on success and -1 in case of an error.

### usrsctp_listen()

```c
int
usrsctp_listen(struct socket *so,
               int backlog)
```

* so: Pointer to the socket as returned by `usrsctp_socket()`.
* backlog: If backlog is non-zero, enable listening, else disable listening.

`usrsctp_listen()` returns 0 on success and -1 in case of an error.

### usrsctp_accept()

```c
struct socket *
usrsctp_accept(struct socket *so,
               struct sockaddr * addr,
               socklen_t * addrlen)
```

* so: Pointer to the socket as returned by `usrsctp_socket()`.
* addr: On return,  the primary address of the peer (`struct sockaddr_in` for an IPv4 address or `struct sockaddr_in6` for an IPv6 address).
* addrlen: Size of the returned address structure.

`usrsctp_accept()` returns the accepted socket on success and NULL in case of an error.

### usrsctp_connect()

```c
int
usrsctp_connect(struct socket *so,
                struct sockaddr *name,
                socklen_t addrlen)
```

* so: Pointer to the socket as returned by `usrsctp_socket()`.
* name: Address of the peer to connect to (`struct sockaddr_in` for an IPv4 address or `struct sockaddr_in6` for an IPv6 address).
* addrlen: Size of the peer's address.

`usrsctp_connect()` returns 0 on success and -1 in case of an error.

### usrsctp_shutdown()

```c
int
usrsctp_shutdown(struct socket *so, int how)
```

* so: Pointer to the socket of the association to be closed
* how: Specifies the type of shutdown.  The values are as follows:
  * SHUT_RD:  Disables further receive operations.  No SCTP protocol action is taken.
  * SHUT_WR:  Disables further send operations, and initiates the SCTP shutdown sequence.
  * SHUT_RDWR:  Disables further send and receive operations, and initiates the SCTP shutdown sequence.

`usrsctp_shutdown()` returns 0 on success and -1 in case of an error.

## Sending and Receiving Data
Since the publication of [RFC 6458](https://tools.ietf.org/html/rfc6458) there is only one function for sending and one for receiving
that is not deprecated. Therefore, only these two are described here.

### usrsctp_sendv()

```c
ssize_t
usrsctp_sendv(struct socket *so,
              const void *data,
              size_t len,
              struct sockaddr *addrs,
              int addrcnt,
              void *info,
              socklen_t infolen,
              unsigned int infotype,
              int flags)
```

* so: The socket to send data on.
* data: As it is more convenient to send data in a buffer and not a `struct iovec` data structure, we chose to pass the data as a void pointer.
* len: Length of the data.
* addrs: In this version of usrsctp at most one destination address is supported. In the case of a connected socket, the parameter `addrs` can be set to NULL.
* addrcnt: Number of addresses. As at most one address is supported, addrcnt is 0 if addrs is NULL and 1 otherwise.
* info: Additional information for a message is stored in `void *info`. The data types `struct sctp_sndinfo`, `struct sctp_prinfo`, and `struct sctp_sendv_spa` are supported as defined in [RFC 6458](https://tools.ietf.org/html/rfc6458). Support for `struct sctp_authinfo` is not implemented yet, therefore, errno is set EINVAL and -1 will be returned, if it is used.
* infolen: Length of info in bytes.
* infotype: Identifies the type of the information provided in info. Possible values are
  * SCTP_SENDV_NOINFO
  * SCTP_SENDV_SNDINFO
  * SCTP_SENDV_PRINFO
  * SCTP_SENDV_SPA (For additional information please refer to [RFC 6458](https://tools.ietf.org/html/rfc6458).)
* flags: Flags as described in [RFC 6458](https://tools.ietf.org/html/rfc6458).

`usrsctp_sendv()` returns the number of bytes sent, or -1 if an error occurred.  The variable errno is then set appropriately.

### usrsctp_recvv()

```c
ssize_t
usrsctp_recvv(struct socket *so,
             void *dbuf,
             size_t len,
             struct sockaddr *from,
             socklen_t * fromlen,
             void *info,
             socklen_t *infolen,
             unsigned int *infotype,
             int *msg_flags)
```

* so: The socket to receive data on.
* dbuf: Analog to `usrsctp_sendv()` the data is returned in a buffer.
* len: Length of the buffer in bytes.
* from: A pointer to an address to be filled with the sender of the received message's address.
* fromlen: An in/out parameter describing the from length.
* info: A pointer to the buffer to hold the attributes of the received message.  The structure type of info is determined by the infotype parameter. The attributes returned in `info` have to be handled in the same way as specified in [RFC 6458](https://tools.ietf.org/html/rfc6458).
* infolen:  An in/out parameter describing the size of the info buffer.
* infotype:  On return, `*infotype` is set to the type of the info buffer.  The current defined values are
  * SCTP_RECVV_NOINFO
  * SCTP_RECVV_RCVINFO
  * SCTP_RECVV_NXTINFO
  * SCTP_RECVV_RN (A detailed description is given in [RFC 6458](https://tools.ietf.org/html/rfc6458))
* flags: A pointer to an integer to be filled with any message flags (e.g., `MSG_NOTIFICATION`).  Note that this field is an in/out parameter.  Options for the receive may also be passed into the value (e.g., `MSG_EOR`).  Returning from the call, the flags' value will differ from its original value.

`usrsctp_recvv()` returns the number of bytes received, or -1 if an error occurred.  The variable errno is then set appropriately.

## Socket Options
Socket options are used to change the default behavior of socket calls.
Their behavior is specified in [RFC 6458](https://tools.ietf.org/html/rfc6458). The functions to get or set them are

```c
int
usrsctp_getsockopt(struct socket *so,
                     int level,
                     int optname,
                     void *optval,
                     socklen_t *optlen)
```
and
```c
int
usrsctp_setsockopt(struct socket *so,
                     int level,
                     int optname,
                     const void *optval,
                     socklen_t optlen)
```

and the arguments are
* so:  The socket of type struct socket.
* level:  Set to IPPROTO_SCTP for all SCTP options.
* optname:  The option name as specified in The Socket Options table below.
* optval: The buffer to store the value of the option as specified in the second column of Socket Options below.
* optlen:  The size of the buffer (or the length of the option returned in case of `usrsctp_getsockopt`).

These functions return 0 on success and -1 in case of an error.

### Socket Options supported by usrsctp

Option | Datatype | r/w
------ | -------- | ----
SCTP_RTOINFO | struct sctp_rtoinfo | r/w
SCTP_ASSOCINFO | struct sctp_assocparams | r/w
SCTP_INITMSG | struct sctp_initmsg | r/w
SCTP_NODELAY | int | r/w
SCTP_AUTOCLOSE | int | r/w
SCTP_PRIMARY_ADDR | struct sctp_setprim | r/w
SCTP_ADAPTATION_LAYER | struct sctp_setadaptation | r/w
SCTP_DISABLE_FRAGMENTS | int | r/w
SCTP_PEER_ADDR_PARAMS | struct sctp_paddrparams | r/w
SCTP_I_WANT_MAPPED_V4_ADDR | int | r/w
SCTP_MAXSEG | struct sctp_assoc_value | r/w
SCTP_DELAYED_SACK | struct sctp_sack_info | r/w
SCTP_FRAGMENT_INTERLEAVE | int | r/w
SCTP_PARTIAL_DELIVERY_POINT | int | r/w
SCTP_HMAC_IDENT | struct sctp_hmacalgo | r/w
SCTP_AUTH_ACTIVE_KEY | struct sctp_authkeyid | r/w
SCTP_AUTO_ASCONF | int | r/w
SCTP_MAX_BURST | struct sctp_assoc_value | r/w
SCTP_CONTEXT | struct sctp_assoc_value | r/w
SCTP_EXPLICIT_EOR | int | r/w
SCTP_REUSE_PORT | int | r/w
SCTP_EVENT | struct sctp_event | r/w
SCTP_RECVRCVINFO | int | r/w
SCTP_RECVNXTINFO | int | r/w
SCTP_DEFAULT_SNDINFO | struct sctp_sndinfo | r/w
SCTP_DEFAULT_PRINFO | struct sctp_default_prinfo | r/w
SCTP_REMOTE_UDP_ENCAPS_PORT | struct sctp_udpencaps | r/w
SCTP_ENABLE_STREAM_RESET | struct sctp_assoc_value | r/w
SCTP_STATUS | struct sctp_status | r
SCTP_GET_PEER_ADDR_INFO | struct sctp_paddrinfo | r
SCTP_PEER_AUTH_CHUNKS | struct sctp_authchunks | r
SCTP_LOCAL_AUTH_CHUNKS | struct sctp_authchunks | r
SCTP_GET_ASSOC_NUMBER | uint32_t | r
SCTP_GET_ASSOC_ID_LIST | struct sctp_assoc_ids | r
SCTP_RESET_STREAMS | struct sctp_reset_streams | w
SCTP_RESET_ASSOC | struct sctp_assoc_t | w
SCTP_ADD_STREAMS | struct sctp_add_streams | w

Further usage details are described in [RFC 6458](https://tools.ietf.org/html/rfc6458), [RFC 6525](https://tools.ietf.org/html/rfc6525), and [draft-ietf-tsvwg-sctp-udp-encaps-03](https://tools.ietf.org/html/draft-ietf-tsvwg-sctp-udp-encaps-03) (work in progress).

## Sysctl variables

In kernel implementations like for instance FreeBSD, it is possible to change parameters in the operating system. These parameters are called sysctl variables.

In usrsctp applications can set or retrieve these variables with the functions
```c
void usrsctp_sysctl_set_ ## (uint32_t value)
```
and
```c
uint32_t usrsctp_sysctl_get_ ## (void)
```
respectively, where `##` stands for the name of the variable.

In the following paragraphs a short description of the parameters will be given.

## Manipulate Memory
#### usrsctp_sysctl_set_sctp_sendspace()
The space of the available send buffer can be changed from its default value of 262,144 bytes to a value between 0 and `2^32 - 1` bytes.

#### usrsctp_sysctl_set_sctp_recvspace()
The space of the available receive buffer can be changed from its default value of 262,144 bytes to a value between 0 and `2^32 - 1` bytes.

#### usrsctp_sysctl_set_sctp_hashtblsize()
The TCB (Thread Control Block) hash table sizes, i.e. the size of one TCB in the hash table, can be tuned between 1 and `2^32 - 1` bytes. The default value is 1,024 bytes. A TCB contains for instance pointers to the socket, the endpoint, information about the association and some statistic data.

#### usrsctp_sysctl_set_sctp_pcbtblsize()
The PCB (Protocol Control Block) hash table sizes, i.e. the size of one PCB in the hash table, can be tuned between 1 and `2^32 - 1` bytes. The default value is 256 bytes. The PCB contains all variables that characterize an endpoint.

#### usrsctp_sysctl_set_sctp_system_free_resc_limit()
This parameters tunes the maximum number of cached resources in the system. It can be set between 0 and `2^32 - 1`. The default value is 1000.

#### usrsctp_sysctl_set_sctp_asoc_free_resc_limit()
This parameters tunes the maximum number of cached resources in an association. It can be set between 0 and `2^32 - 1`. The default value is 10.

#### usrsctp_sysctl_set_sctp_mbuf_threshold_count()
Data is stored in mbufs. Several mbufs can be chained together. The maximum number of small mbufs in a chain can be set with this parameter, before an mbuf cluset is used. The default is 5.

#### usrsctp_sysctl_set_sctp_add_more_threshold()
TBD
This parameter configures the threshold below which more space should be added to a socket send buffer. The default value is 1452 bytes.


## Configure RTO
The retransmission timeout (RTO), i.e. the time that controls the retransmission of messages, has several parameters, that can be changed, for example to shorten the time, before a message is retransmitted. The range of these parameters is between 0 and `2^32 - 1`ms.

#### usrsctp_sysctl_set_sctp_rto_max_default()
The default value for the maximum retransmission timeout in ms is 60,000 (60secs).

#### usrsctp_sysctl_set_sctp_rto_min_default()
The default value for the minimum retransmission timeout in ms is 1,000 (1sec).

#### usrsctp_sysctl_set_sctp_rto_initial_default()
The default value for the initial retransmission timeout in ms is 3,000 (3sec). This value is only needed before the first calculation of a round trip time took place.

#### usrsctp_sysctl_set_sctp_init_rto_max_default()
The default value for the maximum retransmission timeout for an INIT chunk in ms is 60,000 (60secs).


## Set Timers
#### usrsctp_sysctl_set_sctp_valid_cookie_life_default()
A cookie has a specified life time. If it expires the cookie is not valid any more and an ABORT is sent. The default value in ms is 60,000 (60secs).

#### usrsctp_sysctl_set_sctp_heartbeat_interval_default()
Set the default time between two heartbeats. The default is 30,000ms.

#### usrsctp_sysctl_set_sctp_shutdown_guard_time_default()
If a SHUTDOWN is not answered with a SHUTDOWN-ACK while the shutdown guard timer is still running, the association will be aborted after the default of 180secs.

#### usrsctp_sysctl_set_sctp_pmtu_raise_time_default()
TBD
To set the size of the packets to the highest value possible, the maximum transfer unit (MTU) of the complete path has to be known. The default time interval for the path mtu discovery is 600secs.

#### usrsctp_sysctl_set_sctp_secret_lifetime_default()
TBD
The default secret lifetime of a server is 3600secs.

#### usrsctp_sysctl_set_sctp_vtag_time_wait()
TBD
Vtag time wait time, 0 disables it. Default: 60secs


## Set Failure Limits
Transmissions and retransmissions of messages might fail. To protect the system against too many retransmissions, limits have to be defined.

#### usrsctp_sysctl_set_sctp_init_rtx_max_default()
The default maximum number of retransmissions of an INIT chunks is 8, before an ABORT is sent.

#### usrsctp_sysctl_set_sctp_assoc_rtx_max_default()
This parameter sets the maximum number of failed retransmissions before the association is aborted. The default value is 10.

#### usrsctp_sysctl_set_sctp_path_rtx_max_default()
This parameter sets the maximum number of path failures before the association is aborted. The default value is 5. Notice that the number of paths multiplied by this value should be equal to `sctp_assoc_rtx_max_default`. That means that the default configuration is good for two paths.

#### usrsctp_sysctl_set_sctp_max_retran_chunk()
The parameter configures how many times an unlucky chunk can be retransmitted before the association aborts. The default is set to 30.

#### usrsctp_sysctl_set_sctp_path_pf_threshold()
TBD
Default potentially failed threshold. Default: 65535		

#### usrsctp_sysctl_set_sctp_abort_if_one_2_one_hits_limit()
TBD
When one-2-one hits qlimit abort. Default: 0


## Control the Sending of SACKs
#### usrsctp_sysctl_set_sctp_sack_freq_default()
The SACK frequency defines the number of packets that are awaited, before a SACK is sent. The default value is 2.

#### usrsctp_sysctl_set_sctp_delayed_sack_time_default()
As a SACK (Selective Acknowledgment) is sent after every other packet, a timer is set to send a SACK in case another packet does not arrive in due time. The default value for this timer is 200ms.

#### usrsctp_sysctl_set_sctp_strict_sacks()
TBD
This is a flag to turn the controlling of the coherence of SACKs on or off. The default value is 1 (on).

#### usrsctp_sysctl_set_sctp_nr_sack_on_off()
If a slow hosts receives data on a lossy link it is possible that its receiver window is full and new data can only be accepted if one chunk with a higher TSN (Transmission Sequence Number) that has previously been acknowledged is dropped. As a consequence the sender has to store data, even if they have been acknowledged in case they have to be retransmitted. If this behavior is not necessary, non-renegable SACKs can be turned on. By default the use of non-renegable SACKs is turned off.

#### usrsctp_sysctl_set_sctp_enable_sack_immediately()
In some cases it is not desirable to wait for the SACK timer to expire before a SACK is sent. In these cases a bit called SACK-IMMEDIATELY (see [draft-tuexen-tsvwg-sctp-sack-immediately-09](https://tools.ietf.org/html/draft-tuexen-tsvwg-sctp-sack-immediately-09)) can be set to provoke the instant sending of a SACK. The default is to turn it off.

#### usrsctp_sysctl_set_sctp_L2_abc_variable()
TBD
SCTP ABC max increase per SACK (L). Default: 1

## Change Max Burst
Max burst defines the maximum number of packets that may be sent in one flight.

#### usrsctp_sysctl_set_sctp_max_burst_default()
The default value for max burst is 0, which means that the number of packets sent as a flight is not limited by this parameter, but may be by another one, see the next paragraph.

#### usrsctp_sysctl_set_sctp_use_cwnd_based_maxburst()
The use of max burst is based on the size of the congestion window (cwnd). This parameter is set by default.

#### usrsctp_sysctl_set_sctp_hb_maxburst()
Heartbeats are mostly used to verify a path. Their number can be limited. The default is 4.

#### usrsctp_sysctl_set_sctp_fr_max_burst_default()
In the state of fast retransmission the number of packet bursts can be limited. The default value is 4.


## Handle Chunks
#### usrsctp_sysctl_set_sctp_peer_chunk_oh()
In order to keep track of the peer's advertised receiver window, the sender calculates the window by subtracting the amount of data sent. Yet, some OSs reduce the receiver window by the real space needed to store the data. This parameter sets the additional amount to debit the peer's receiver window per chunk sent. The default value is 256, which is the value needed by FreeBSD.

#### usrsctp_sysctl_set_sctp_max_chunks_on_queue()
This parameter sets the maximum number of chunks that can be queued per association. The default value is 512.

#### usrsctp_sysctl_set_sctp_min_split_point()
TBD
The minimum size when splitting a chunk is 2904 bytes by default.

#### usrsctp_sysctl_set_sctp_chunkscale()
TBD
This parameter can be tuned for scaling of number of chunks and messages. The default is10.

#### usrsctp_sysctl_set_sctp_min_residual()
TBD
This parameter configures the minimum size of the residual data chunk in the second part of the split. The default is 1452.


## Calculate RTT
The calculation of the round trip time (RTT) depends on several parameters.

#### usrsctp_sysctl_set_sctp_rttvar_bw()
TBD
Shift amount for bw smoothing on rtt calc. Default: 4

#### usrsctp_sysctl_set_sctp_rttvar_rtt()
TBD
Shift amount for rtt smoothing on rtt calc. Default: 5

#### usrsctp_sysctl_set_sctp_rttvar_eqret()
TBD
What to return when rtt and bw are unchanged. Default: 0


## Influence the Congestion Control
The congestion control should protect the network against fast senders.

#### usrsctp_sysctl_set_sctp_ecn_enable
Explicit congestion notifications are turned on by default.

#### usrsctp_sysctl_set_sctp_default_cc_module()
This parameter sets the default algorithm for the congestion control. Default is 0, i.e. the one specified in [RFC 4960](https://tools.ietf.org/html/rfc4960).

#### usrsctp_sysctl_set_sctp_initial_cwnd()
Set the initial congestion window in MTUs. The default is 3.

#### usrsctp_sysctl_set_sctp_use_dccc_ecn()
TBD
Enable for RTCC CC datacenter ECN. Default: 1

#### usrsctp_sysctl_set_sctp_steady_step()
TBD
How many the sames it takes to try step down of cwnd. Default: 20


## Configure AUTH and ADD-IP
An important extension of SCTP is the dynamic address reconfiguration (see [RFC 5061](https://tools.ietf.org/html/rfc5061)), also known as ADD-IP, which allows the changing of addresses during the lifetime of an association. For this feature the AUTH extension (see [RFC 4895](https://tools.ietf.org/html/rfc4895)) is necessary.

#### usrsctp_sysctl_set_sctp_auto_asconf()
If SCTP Auto-ASCONF is enabled, the peer is informed automatically when a new address
is added or removed. This feature is enabled by default.

#### usrsctp_sysctl_set_sctp_multiple_asconfs()
By default the sending of multiple ASCONFs is disabled.

#### usrsctp_sysctl_set_sctp_auth_enable()
The use of AUTH, which is normally turned on, can be disabled by setting this parameter to 0.

#### usrsctp_sysctl_set_sctp_asconf_auth_nochk()
It is also possible to disable the requirement to use AUTH in conjunction with ADD-IP by setting this parameter
to 1.


## Concurrent Multipath Transfer (CMT)
A prominent feature of SCTP is the possibility to use several addresses for the same association. One is the primary path, and the others are needed in case of a path failure. Using CMT the data is sent on several paths to enhance the throughput.

#### usrsctp_sysctl_set_sctp_cmt_on_off()
To turn CMT on, this parameter has to be set to 1.

#### usrsctp_sysctl_set_sctp_cmt_use_dac()
To use delayed acknowledgments with CMT this parameter has to be set to 1.

#### usrsctp_sysctl_set_sctp_buffer_splitting()
For CMT it makes sense to split the send and receive buffer to have shares for each path. By default buffer splitting is turned off.


## Network Address Translation (NAT)
To be able to pass NAT boxes, the boxes have to handle SCTP packets in a specific way.

#### usrsctp_sysctl_set_sctp_nat_friendly()
SCTP NAT friendly operation. Default:1

#### usrsctp_sysctl_set_sctp_inits_include_nat_friendly()
Enable sending of the nat-friendly SCTP option on INITs. Default: 0

#### usrsctp_sysctl_set_sctp_udp_tunneling_port()
Set the SCTP/UDP tunneling port. Default: 9899

## SCTP Mobility
#### usrsctp_sysctl_set_sctp_mobility_base()
TBD
Enable SCTP base mobility. Default: 0


#### usrsctp_sysctl_set_sctp_mobility_fasthandoff()
TBD
Enable SCTP fast handoff. default: 0


## Miscellaneous
#### usrsctp_sysctl_set_sctp_no_csum_on_loopback()
Calculating the checksum for packets sent on loopback is turned off by default. To turn it on, set this parameter to 0.

#### usrsctp_sysctl_set_sctp_nr_outgoing_streams_default()
The peer is notified about the number of outgoing streams in the INIT or INIT-ACK chunk. The default is 10.

#### usrsctp_sysctl_set_sctp_do_drain()
Determines whether SCTP should respond to the drain calls. Default: 1		

#### usrsctp_sysctl_set_sctp_strict_data_order()
TBD
Enforce strict data ordering, abort if control inside data. Default: 0

#### usrsctp_sysctl_set_sctp_default_ss_module()
Set the default stream scheduling module. Implemented modules are:
* SCTP_SS_DEFAULT
* SCTP_SS_ROUND_ROBIN
* SCTP_SS_ROUND_ROBIN_PACKET
* SCTP_SS_PRIORITY
* SCTP_SS_FAIR_BANDWITH
* SCTP_SS_FIRST_COME

#### usrsctp_sysctl_set_sctp_default_frag_interleave()
TBD
Default fragment interleave level. Default: 1

#### usrsctp_sysctl_set_sctp_blackhole()
TBD
Enable SCTP blackholing. Default: 0

#### usrsctp_sysctl_set_sctp_logging_level()
Set the logging level. The default is 0.

#### usrsctp_sysctl_set_sctp_debug_on()
Turn debug output on or off. It is disabled by default. To obtain debug output, `SCTP_DEBUG` has to be set as a compile flag.


### sysctl variables supported by usrsctp

Parameter | Meaning | Default Value
--------- | ------- | -------------
sctp_sendspace | Send buffer space | 1864135
sctp_recvspace | Receive buffer space | 1864135
sctp_hashtblsize | Tunable for TCB hash table sizes | 1024
sctp_pcbtblsize | Tunable for PCB hash table sizes | 256
sctp_system_free_resc_limit | Cached resources in the system | 1000
sctp_asoc_free_resc_limit | Cashed resources in an association | 10
sctp_rto_max_default | Default value for RTO_max | 60000ms
sctp_rto_min_default | Default value for RTO_min | 1000ms
sctp_rto_initial_default | Default value for RTO_initial | 3000ms
sctp_init_rto_max_default | Default value for the maximum RTO for sending an INIT | 60000ms
sctp_valid_cookie_life_default | Valid cookie life time | 60000ms
sctp_init_rtx_max_default | Maximum number of INIT retransmissions | 8
sctp_assoc_rtx_max_default | Maximum number of failed retransmissions before the association is aborted | 10
sctp_path_rtx_max_default | Maximum number of failed retransmissions before a path fails | 5
sctp_ecn_enable | Enabling explicit congestion notifications | 1
sctp_strict_sacks | Control the coherence of SACKs | 1
sctp_delayed_sack_time_default | Default delayed SACK timer | 200ms
sctp_sack_freq_default | Default SACK frequency | 2
sctp_nr_sack_on_off | Turn non-renegable SACKs on or off | 0
sctp_enable_sack_immediately | Enable sending of the SACK-IMMEDIATELY bit | 0
sctp_no_csum_on_loopback | Enable the compilation of the checksum on packets sent on loopback | 1
sctp_peer_chunk_oh | Amount to debit peers rwnd per chunk sent | 256
sctp_max_burst_default | Default max burst for SCTP endpoints | 0
sctp_use_cwnd_based_maxburst | Use max burst based on the size of the congestion window | 1
sctp_hb_maxburst | Confirmation Heartbeat max burst | 4
sctp_max_chunks_on_queue | Default max chunks on queue per asoc | 512
sctp_min_split_point | Minimum size when splitting a chunk | 2904
sctp_chunkscale | Tunable for Scaling of number of chunks and messages | 10
sctp_mbuf_threshold_count | Maximum number of small mbufs in a chain | 5
sctp_heartbeat_interval_default | Default time between two Heartbeats | 30000ms
sctp_pmtu_raise_time_default | Default PMTU raise timer | 600secs
sctp_shutdown_guard_time_default | Default shutdown guard timer | 180secs
sctp_secret_lifetime_default | Default secret lifetime | 3600secs
sctp_add_more_threshold | Threshold when more space should be added to a socket send buffer | 1452
sctp_nr_outgoing_streams_default | Default number of outgoing streams | 10
sctp_cmt_on_off | Turn CMT on or off. | 0
sctp_cmt_use_dac | Use delayed acknowledgment for CMT | 0
sctp_fr_max_burst_default | Default max burst for SCTP endpoints when fast retransmitting | 4
sctp_auto_asconf | Enable SCTP Auto-ASCONF | 1
sctp_multiple_asconfs | Enable SCTP Multiple-ASCONFs | 0
sctp_asconf_auth_nochk | Disable SCTP ASCONF AUTH requirement | 0
sctp_auth_disable | Disable SCTP AUTH function | 0
sctp_nat_friendly | SCTP NAT friendly operation | 1
sctp_inits_include_nat_friendly | Enable sending of the nat-friendly SCTP option on INITs. | 0
sctp_udp_tunneling_port | Set the SCTP/UDP tunneling port | 9899
sctp_do_drain | Determines whether SCTP should respond to the drain calls | 1
sctp_abort_if_one_2_one_hits_limit | When one-2-one hits qlimit abort | 0
sctp_strict_data_order | Enforce strict data ordering, abort if control inside data | 0
sctp_min_residual | Minimum residual data chunk in second part of split | 1452
sctp_max_retran_chunk | Maximum times an unlucky chunk can be retransmitted before the association aborts | 30
sctp_default_cc_module | Default congestion control module | 0
sctp_default_ss_module | Default stream scheduling module | 0
sctp_default_frag_interleave | Default fragment interleave level | 1
sctp_mobility_base | Enable SCTP base mobility | 0
sctp_mobility_fasthandoff | Enable SCTP fast handoff | 0
sctp_L2_abc_variable | SCTP ABC max increase per SACK (L) | 1
sctp_vtag_time_wait | Vtag time wait time, 0 disables it. | 60secs
sctp_blackhole | Enable SCTP blackholing | 0
sctp_path_pf_threshold | Default potentially failed threshold | 65535
sctp_rttvar_bw | Shift amount for bw smoothing on rtt calc | 4
sctp_rttvar_rtt | Shift amount for rtt smoothing on rtt calc | 5
sctp_rttvar_eqret  | What to return when rtt and bw are unchanged | 0
sctp_steady_step | How many the sames it takes to try step down of cwnd | 20
sctp_use_dccc_ecn | Enable for RTCC CC datacenter ECN | 1
sctp_buffer_splitting | Enable send/receive buffer splitting | 0
sctp_initial_cwnd | Initial congestion window in MTUs | 3
sctp_logging_level | Logging level | 0
sctp_debug_on | Turns debug output on or off. | 0

## Examples

See https://github.com/sctplab/usrsctp/tree/master/programs


## References

#### SCTP
R. Stewart:</br>
`Stream Control Transmission Protocol`.</br>
[RFC 4960](https://tools.ietf.org/html/rfc4960), September 2007.

#### auth
M. Tüxen, R. Stewart, P. Lei, and E. Rescorla:</br>
`Authenticated Chunks for the Stream Control Transmission Protocol (SCTP)`.</br>
[RFC 4895](https://tools.ietf.org/html/rfc4895), August 2007.

#### addip
R. Stewart, Q. Xie, M. Tüxen, S. Maruyama, and M. Kozuka:</br>
`Stream Control Transmission Protocol (SCTP) Dynamic Address Reconfiguration`.</br>
[RFC 5061](https://tools.ietf.org/html/rfc5061), September 2007.

#### socketAPI
R. Stewart, M. Tüxen, K. Poon, and V. Yasevich:</br>
`Sockets API Extensions for the Stream Control Transmission Protocol (SCTP)`.</br>
[RFC 6458](https://tools.ietf.org/html/rfc6458), Dezember 2011.

#### streamReset
R. Stewart, M. Tüxen, and P. Lei:</br>
`Stream Control Transmission Protocol (SCTP) Stream Reconfiguration`.</br>
[RFC 6525](https://tools.ietf.org/html/rfc6525), February 2012.

#### udpencaps
M. Tüxen and R. Stewart</br>
`UDP Encapsulation of Stream Control Transmission Protocol (SCTP) Packets for End-Host to End-Host Communication`</br>
[RFC 6951](https://tools.ietf.org/html/rfc6951), May 2013.

#### sack-imm
M. Tüxen, I. Rüngeler, and R. Stewart:</br>
`SACK-IMMEDIATELY Extension for the Stream Control Transmission Protocol`</br>
[RFC 7053](https://tools.ietf.org/html/rfc7053), November 2013.
