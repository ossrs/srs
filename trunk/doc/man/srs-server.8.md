---
title: SRS-SERVER
section: 8
date: 30 July, 2023
---

# NAME

**srs-server** - a video server, supports RTMP/WebRTC/HLS/HTTP-FLV/SRT.

# SYNOPSIS

**srs-server** \<-h?vVgGe\>\|\<\[-t\] -c filename\>

# DESCRIPTION

SRS is a simple, high efficiency and realtime video server, supports RTMP/WebRTC/HLS/HTTP-FLV/SRT.

The options are as follows:

Options: 

-?, -h, --help : Show this help and exit 0.\
-v, -V, --version : Show version and exit 0.\
-g, -G : Show server signature and exit 0. -e : Use environment variable only, ignore config file.\
-t : Test configuration file, exit with error code(0 for success).\
-c filename : Use config file to start server.\

# FILES

/run/srs-server.pid

    Contains the process ID of srs-server.  The contents of this file are not sensitive, so it can be world-readable.
	
/etc/srs-server/srs.conf

    The main configuration file.
        

# EXIT STATUS

Exit status is 0 on success, or 1 if the command fails.

# EXAMPLE
For example:

    srs-server -v
	srs-server -t -c conf/srs.conf
	srs-server -c conf/srs.conf

# SEE ALSO

Documentation at https://ossrs.io/lts/en-us/docs/v5/doc/getting-started.

# HISTORY

Development of srs-server started in 2013.

# AUTHORS

Winlin<winlin@vip.126.com>\
ZhaoWenjie<zhaowenjie@tal.com>\
ShiWei<shiwei05@kuaishou.com>\
XiaoZhihong<hondaxiao@tencent.com>\
WuPengqiang<pengqiang.wpq@alibaba-inc.com>\
XiaLixin<xialixin@kanzhun.com>\
LiPeng<mozhan.lp@alibaba-inc.com>\
ChenGuanghua<jinxue.cgh@alibaba-inc.com>\
ChenHaibo<nmgchenhaibo@foxmail.com>\
and https://github.com/ossrs/srs/blob/develop/trunk/AUTHORS.md#contributors


