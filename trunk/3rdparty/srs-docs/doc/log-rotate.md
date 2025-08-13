---
title: Log Rotate
sidebar_label: Log Rotate
hide_title: false
hide_table_of_contents: false
---

# LogRotate

SRS always writes log to a single log file `srs.log`, so it will become very larger. We can use rotate the log to zip or remove it.

1. First, move the log file to another tmp log file:```mv objs/srs.log /tmp/srs.`date +%s`.log```
1. Then, send signal to SRS. SRS will close the previous file fd and reopen the log file:```killall -s SIGUSR1```
1. Finally, zip or remove the tmp log file.

## Use logrotate

Recommend to use [logrotate](https://www.jianshu.com/p/ec7f1626a3d3) to manage log files.

1. Install logrotate:

```
sudo yum install -y logrotate
```

1. Config logrotate to manage SRS log file:

```
cat << END > /etc/logrotate.d/srs
/usr/local/srs/objs/srs.log {
    daily
    dateext
    compress
    rotate 7
    size 1024M
    sharedscripts
    postrotate
        kill -USR1 \`cat /usr/local/srs/objs/srs.pid\`
    endscript
}
END
```

> Note: Run logrotate manually by `logrotate -f /etc/logrotate.d/srs`

## CopyTruncate

For SRS2, we could use [copytruncate](https://unix.stackexchange.com/questions/475524/how-copytruncate-actually-works),
**but it's strongly not recommended** because the logs maybe dropped, so it's only a workaround for server not supported
SIGUSR1 such as SRS2.

> Yes, SRS3 surely supports copytruncate and it's not recommended.

The config is bellow, from [PR#1561](https://github.com/ossrs/srs/pull/1561#issuecomment-571408173) by [wnpllrzodiac](https://github.com/wnpllrzodiac):

```
cat << END > /etc/logrotate.d/srs
/usr/local/srs/objs/srs.log {
    daily
    dateext
    compress
    rotate 7
    size 1024M
    copytruncate
}
END
```

Winlin 2016.12

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/log-rotate)


