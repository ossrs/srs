针对Android 11之后对访问系统MAC地址的权限进行了修改的问题，导致普通应用在调用`NETLINK`套接字时会遇到以下几个主要问题：
- 不允许对`NETLINK`套接字进行`bind`操作。
- 不允许调用`RTM_GETLINK`功能。

详细说明可以在此链接找到：https://developer.android.com/training/articles/user-data-ids#mac-11-plus

由于上述两个原因，导致在安卓环境下使用Go net包中的`net.Interfaces()`和`net.InterfaceAddrs()`时会抛出`route ip+net: netlinkrib: permission denied`错误。
具体 issue 可见：https://github.com/golang/go/issues/40569

为了解决在安卓环境下使用Go net包的问题，我们对其源代码进行了部分改造，以使其能够在Android上正常工作。

对于`net.InterfaceAddrs()`，我已经完全解决了其中的问题；
对于`net.Interfaces()`，我只解决了部分问题，目前仍存在以下问题：
- 只能返回具有IP地址的接口。
- 不能返回硬件的MAC地址。

但是修复后的`net.Interfaces()`函数现在与Android API的`NetworkInterface.getNetworkInterfaces()`保持一致，在大多数情况下可正常使用。

具体修复逻辑包括：

- 取消了`NetlinkRIB()`函数中对`Netlink`套接字的`Bind()`操作。
- 根据`RTM_GETADDR`返回的Index号，使用`ioctl`获取其网卡的名称、MTU和标志位。

## 由于 #40569 导致的其他问题
- #[68082](https://github.com/golang/go/issues/68082)