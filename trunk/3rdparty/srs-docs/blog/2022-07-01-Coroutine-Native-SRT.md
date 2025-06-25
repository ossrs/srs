---
slug: coroutine-native-srt
title: SRS - Coroutine Native for SRT
authors: []
tags: [tutorial, video, srt, streaming]
custom_edit_url: null
---

# Coroutine Native SRT

> Written by [John](https://github.com/xiaozhihong), [Winlin](https://github.com/winlinvip)

Coroutines are core technologies for modern servers that significantly simplify the logic and facilitate maintenance. SRT is a new streaming protocol that is gradually taking over from RTMP. With its own I/O framework, SRT can mature only by becoming coroutine-native, the first and crucial step of SRS 5.0.

<!--truncate-->

Since the beginning of 2022, we have explored, discussed, and finally determined the media gateway as the fundamental direction of SRS 5.0, as detailed in [Definitions and Solutions of Core Issues with SRS 5.0](https://mp.weixin.qq.com/s/Rq3NuZKdMr2_Dmiki72RFw).

Though popular with live streaming/broadcasting push, SRT is not supported by web browsers. And that's where the media gateway comes into play. Specifically, it converts SRT into RTMP/HLS/WebRTC to deliver an ultra low-latency broadcasting solution that keeps SRT's powerful cross-border transfer capabilities.

The foundation lies in coroutine-native SRT, today's topic. It will have a far-reaching impact on SRS 5.0. For more information on its code, see [PR#3010](https://github.com/ossrs/srs/pull/3010).

First of all, let's talk about its background.

## Introduction

RTMP is widely used in the live push field. It is also one of the most compatible protocols with different live streaming origin servers. 

With an increasing number of scenarios and the progress of live streaming, some of the acute problems have emerged:

1. TCP push performs poorly in terms of packet loss, RTT, and jitter for long-distance transfer.
1. RTMP doesn't support multiple audio tracks or new coding standards such as H.265 and AV1.
1. Adobe has abandoned RTMP, which has not been updated for years nor will it be updated in the future.

To solve these problems, the broadcasting field has widely adopted SRT push since 2018, and more and more push devices and platforms have followed suit.

At the end of 2019, SRT push became supported by SRS 4.0. However, there were several shortcomings:

1. SRT is implemented in SRS with multiple threads in an asynchronous manner, making it hard to troubleshoot program crashes due to exceptions.
1. SRT is implemented in SRS asynchronously, leading to complex code and maintenance.
1. SRT playback won't take effect after the HTTP callback, which is triggered by RTMP after the SRT push dependency turns to RTMP. There is no callback for playback.
1. SRT needs to be converted into RTMP first before it can be converted into WebRTC, causing a high latency.

The core cause of these problems is that SRT uses independent and asynchronous I/O and multiple threads that cannot be combined with the existing ST coroutines of SRS.

Therefore, SRT must become coroutine-native and fall into the same ST coroutine framework as SRS. This has been achieved in SRS 5.0. For more information on its code, see [PR#3010](https://github.com/ossrs/srs/pull/3010). This is an extremely important feature.

Before moving to the coroutine-native SRT, let's take a look at coroutine native itself, as exemplified by the coroutine-native TCP of ST.

## Coroutine Native TCP

To begin with, let's see the following logic of the non-coroutine-native code, that is, async code driven by epoll:

```cpp
int fd = accept(listen_fd); // Got a TCP connection.

int n = read(fd, buf, sizeof(buf));
if (n == -1) {
  if (errno == EAGAIN) { // Not ready
    return epoll_ctl(fd, EPOLLIN); // Wait for fd to be ready.
  }
  return n; // Error.
}

printf("Got %d size of data %p", n, buf);
```

> Note: The above is just a sample that helps better express the key logic. You can see epoll sample code for more details.

Generally speaking, read is a business logic, and the data that is read is business data. However, the underlying framework of epoll needs to be called here for fd processing during EAGAIN. As a result, the underlying logic and business logic are mixed, complicating maintenance.

> Note: Although NGINX has a packaged framework layer, async callback still exists. If the current function needs to be returned before the fd is ready, many statuses need to be stored and recovered. When the logic is complex, the state machine becomes complicated.

See the following code to see what a coroutine-native logic will be like in this case:

```cpp
st_netfd_t fd = st_accept(listen_fd); // Got a TCP connection

int n = st_read(fd, buf, sizeof(buf));
if (n == -1) {
    return n; // Error.
}

printf("Got %d size of data %p", n, buf);
```

Obviously, there seems to be no EAGAIN. It may be that data is read or an error occurs. No matter what the case is, fd will always be ready. This means no need to store the status and no repeated read attempts, creating a very good experience when using the business logic.

Why is there no EAGAIN for `st_read` when epoll is still used? Well, there is, but it has been handled less obviously with the coroutine. Let's see the `st_readv` function:

```cpp
ssize_t st_read(_st_netfd_t *fd, void *buf, size_t nbyte) {
    while ((n = read(fd->osfd, buf, nbyte)) < 0) {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR) // Error, if not EAGAIN.
            return -1;

        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN) < 0) // EAGAIN
            return -1;
    }
    
    return n;
}
```

During `EAGAIN`, `st_netfd_poll` is called, which switches the current coroutine and schedules the thread to execute the next coroutine. In addition, the current coroutine will be recovered at some point when the I/O event arrives or a timeout occurs.

> Note: Both the coroutine switch and recovery are implemented in the function and imperceptible to the code called at the upper layer. Therefore, there is no EAGAIN error message or return to the upper-layer function, and no need to store and resume the status.

In summary, do the following to make any protocol coroutine-native:

1. Call the API once, which will directly return upon success.
1. In the case of a failure, check for the error and return if the error is irrelevant to I/O wait.
1. Switch the current coroutine and run other coroutines until the arrival of a timeout or the fd event. In the former case, return an error; in the latter case, repeat the above steps.

With that, we can make SRT coroutine-native.

## Coroutine Native SRT

The `srt_recvmsg` function is used as an example. It resembles the `read` function of TCP and is as defined below:

```cpp
SRT_API int srt_recvmsg (SRTSOCKET u, char* buf, int len);
```

Implement `SrsSrtSocket::recvmsg` , another function similar to `st_read` :

```cpp
srs_error_t SrsSrtSocket::recvmsg(void* buf, size_t size, ssize_t* nread) {
  while (true) {
    int ret = srt_recvmsg(srt_fd_, (char*)buf, size);
    if (ret >= 0) { // Receive message ok.
      recv_bytes_ += ret; 
      *nread = ret;
      return err;
    }
    
    // Got something error, return immediately.
    if (srt_getlasterror(NULL) != SRT_EASYNCRCV) {
      return srs_error_new(ERROR_SRT_IO, "srt_recvmsg");
    }
    
    // Wait for the fd ready or error, switch to other coroutines.
    if ((err = wait_readable()) != srs_success) { // EAGAIN.
      return srs_error_wrap(err, "wait readable");
    }
  }
  
  return err;
}
```

As we can see, `wait_readable` implements coroutine switch and recovery just like `st_read` , but with the `st_cond_t` condition variable:

```cpp
srs_error_t SrsSrtSocket::wait_readable() {
  srt_poller_->mod_socket(this, SRT_EPOLL_IN);
  srs_cond_timedwait(read_cond_);
}
```

> Note: The `SRT_EPOLL_IN` event listened by epoll is modified before the fd becomes readable and the condition variable is triggered.

`SrsSrtPoller::wait` is the function that triggers the condition variable. Its implementation is as follows:

```cpp
srs_error_t SrsSrtPoller::wait(int timeout_ms, int* pn_fds) {
  int ret = srt_epoll_uwait(srt_epoller_fd_, events_.data(), events_.size());
  for (int i = 0; i < ret; ++i) {
    if (event.events & SRT_EPOLL_IN) {
      srt_skt->notify_readable();
    }
  }
}

void SrsSrtSocket::notify_readable() {
  srs_cond_signal(read_cond_);
}
```

Now the SRT API becomes coroutine-native. The operation is similar for other APIs such as srt_sendmsg, srt_connnect, and srt_accept.

Let's compare coroutine native and the original callback.

## Coroutine Native PK Callback

After making the SRT coroutine-native, we succeeded in separating the business logic from the underlying code, thereby clarifying the upper-layer code logic.

Let's see the accept logic, where event processing is triggered by epoll to create the `srt_conn` data structure:

```cpp
while (run_flag) {
  int ret = srt_epoll_wait(_pollid, read_fds, &rfd_num, write_fds);
  for (int index = 0; index < rfd_num; index++) {
    SRT_SOCKSTATUS status = srt_getsockstate(read_fds[index]);
    srt_handle_connection(status, read_fds[index], "read fd");
  }
}

void srt_server::srt_handle_connection(SRT_SOCKSTATUS status, SRTSOCKET input_fd) {
  if (status == SRTS_LISTENING) {
    conn_fd = srt_accept(input_fd, (sockaddr*)&scl, &sclen);
    _handle_ptr->add_newconn(conn_fd, SRT_EPOLL_IN);
  }
}

void srt_handle::add_newconn(SRT_CONN_PTR conn_ptr, int events) {
    _push_conn_map.insert(std::make_pair(conn_ptr->get_path(), conn_ptr));
    _conn_map.insert(std::make_pair(conn_ptr->get_conn(), conn_ptr));
    int ret = srt_epoll_add_usock(_handle_pollid, conn_ptr->get_conn(), &events);
}
```

> Note: The created `srt_conn` is stored in the global data structure and continuously modified in the follow-up callback events.

The following is a coroutine-native business logic, which starts the processing coroutine after receiving the session:

```cpp
srs_error_t SrsSrtListener::cycle() {
  while (true) {
    srs_srt_t client_srt_fd = srs_srt_socket_invalid();
    srt_skt_->accept(&client_srt_fd);
    
    srt_server_->accept_srt_client(srt_fd);
  }
}

srs_error_t SrsSrtServer::accept_srt_client(srs_srt_t srt_fd) {
  fd_to_resource(srt_fd, &srt_conn);
  conn_manager_->add(srt_conn);
  srt_conn->start();
}
```

> Note: Although `srt_conn` is maintained by global variables, it is the coroutine-native execution logic—not callback logic or epoll processing—that is relevant.

In the callback logic, you need to view the epoll callback event before maintaining or understanding the code. What's worse, the `srt_conn` status is modified by different events, making it hard to understand the lifecycle of the object; while in the coroutine-native logic, its lifecycle is contained in the coroutine, it is processed in a coroutine after `srt_conn` is received, and further read and write operations are also performed in the coroutine.

Let's move on to the read processing logic of `srt_conn` , where the native read function of SRT is used and the callback is triggered by the epoll event:

```cpp
while (run_flag) {
  int ret = srt_epoll_wait(_pollid, read_fds, &rfd_num, write_fds);
  for (int index = 0; index < rfd_num; index++) {
    SRT_SOCKSTATUS status = srt_getsockstate(read_fds[index]);
    srt_handle_data(status, read_fds[index], "read fd");
  }
}

void srt_handle::handle_srt_socket(SRT_SOCKSTATUS status, SRTSOCKET conn_fd) {
  auto conn_ptr = get_srt_conn(conn_fd);
  int mode = conn_ptr->get_mode();
  if (mode == PUSH_SRT_MODE && status == SRTS_CONNECTED) {
    handle_push_data(status, path, subpath, conn_fd);
  }
}

void srt_handle::handle_push_data(SRT_SOCKSTATUS status, SRTSOCKET conn_fd) {
  srt_conn_ptr = get_srt_conn(conn_fd);
  if (status != SRTS_CONNECTED) { // Error.
    close_push_conn(conn_fd);
    return;
  }

  ret = srt_conn_ptr->read_async(data, DEF_DATA_SIZE);
  if (ret <= 0) { // Error.
    if (srt_getlasterror(NULL) != SRT_EASYNCRCV) {
      return;
    }
    close_push_conn(conn_fd);
    return;
  }

  srt2rtmp::get_instance()->insert_data_message(data, ret, subpath);
}
```

> Note: We need to process a variety of statuses in the callback, but the `srt_conn` status changes are determined by different callbacks, so it is not easy to determine the main processing logic of the session.

A coroutine-native version of the SRT business logic will be like:

```cpp
srs_error_t SrsMpegtsSrtConn::do_publishing() {
  while (true) {
    ssize_t nb = 0;
    if ((err = srt_conn_->read(buf, sizeof(buf), &nb)) != srs_success) {
      return srs_error_wrap(err, "srt: recvmsg");
    }
    
    if ((err = on_srt_packet(buf, nb)) != srs_success) {
      return srs_error_wrap(err, "srt: process packet");
    }
  }
}
```

> Note: The lifecycle of `srt_conn` is clear, and its status is returned in the error within the cycle, which can be regarded as the main loop for this session. It will not enter the epoll loop of SRT due to read, and you can ignore the async event trigger and processing during maintenance.

Again, different information is required for code maintenance in different logics. In the async callback logic, you need to know the statuses of the current object, the modified statuses, and the impact of other async events from the callback function. While in the coroutine-native logic, these statuses are irrelevant, and the creation and execution of the coroutine are linear. Or we can say that these statuses are in the coroutine function callback.

> Note: The async callback status cannot be in the function callback, as the async callback stack cannot store the `srt_conn` status. In essence, it is a coroutine which stores the status of the epoll loop. While a coroutine is created according to each `srt_conn`, and the corresponding `srt_conn` status is stored in its stack.

In fact, the async callback status can only be stored in the global data structure, while the coroutine status can be stored in each local variable. The local variable of each function is unique to the coroutine and can be used as long as the coroutine doesn't end.

## What is the Next

The coroutine-native SRT still faces some problems and requires follow-up actions:

1. Directly convert SRT into WebRTC to lower the latency in live streaming.
1. Replace TCP with SRT for long-linkage transfer between some servers, such as cross-border RTMP forwarding.
1. Improve the SRT tool chain, such as [srs-bench](https://github.com/ossrs/srs-bench), to support stress testing of SRT streams.

Join the SRS open-source community to make a powerful streaming media server available for all.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## One More Thing

In case you may wonder, let me tell you the differences between a commercial video cloud SRT server and an open-source SRT server as well as the optimizations.

> Note: Some optimizations are not suitable for open-source projects, as open-source servers put more emphasis on protocol standardization and compatibility. Therefore, a commercial cloud computing server can be totally different from an open-source server. Even the Linux kernel of the commercial server will greatly differ from that of the open-source server.

Here, some of SRT's most troublesome problems are the high retransmission rate and poorer performance than TCP/QUIC when the bandwidth is limited. Tencent Cloud makes the following optimizations accordingly:

1. SRT retransmission disorder adaptation: When disordered packets are received, the system will wait for N packets before initiating the first retransmission according to the current degree of the disorder. The native SRT disorder has a fixed value, which can be adjusted to be more adaptive to the network disorder conditions.
1. SRT transfer parameter optimization: The retransmission rate is halved after the parameter optimization.
1. Addition of the BBR congestion control algorithm: The native SRT congestion control is weak, and the evaluated bandwidth fluctuates greatly, both of which are resolved by adding the BBR congestion control algorithm.
1. SRT multi-linkage transfer improved with bandwidth aggregation: The auto mode for live streaming is added to SRT, in addition to its native backup and broadcast modes. In this way, the bandwidths of multiple ENIs are aggregated for live streaming, with smart and dynamic linkage selection.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/22-07-01-Coroutine-Native-SRT)


