---
title: Security
sidebar_label: Security
hide_title: false
hide_table_of_contents: false
---

# Security

SRS provides simple security strategy to allow or deny specifies clients.

## Config

The config for security of vhost:

```
vhost your_vhost {
    # security for host to allow or deny clients.
    # @see https://github.com/ossrs/srs/issues/211   
    security {
        # whether enable the security for vhost.
        # default: off
        enabled         on;
        # the security list, each item format as:
        #       allow|deny    publish|play    all|<ip or cidr>
        # for example:
        #       allow           publish     all;
        #       deny            publish     all;
        #       allow           publish     127.0.0.1;
        #       deny            publish     127.0.0.1;
        #       allow           publish     10.0.0.0/8;
        #       deny            publish     10.0.0.0/8;
        #       allow           play        all;
        #       deny            play        all;
        #       allow           play        127.0.0.1;
        #       deny            play        127.0.0.1;
        #       allow           play        10.0.0.0/8;
        #       deny            play        10.0.0.0/8;
        # SRS apply the following simple strategies one by one:
        #       1. allow all if security disabled.
        #       2. default to deny all when security enabled.
        #       3. allow if matches allow strategy.
        #       4. deny if matches deny strategy.
        allow           play        all;
        allow           publish     all;
    }
}
```

Please see `conf/security.deny.publish.conf` for detail.

## Kickoff Client

SRS provides api to kickoff user, read [wiki](./http-api.md#kickoff-client).

## Bug

The bug about this feature, read [#211](https://github.com/ossrs/srs/issues/211)

## Reload

When reload the security config, it only effects the new clients.

Winlin 2015.1

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/security)


