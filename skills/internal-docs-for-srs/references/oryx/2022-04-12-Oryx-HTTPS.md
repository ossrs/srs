---
slug: Oryx-HTTPS
title: Oryx - How to Secure SRS with Let's Encrypt by 1-Click
authors: []
tags: [turotial, srs, https]
custom_edit_url: null
---

# How to Secure SRS with Let's Encrypt by 1-Click

## Introduction

As a CA(Certificate Authority), Let's Encrypt provides free and automatic TLS/SSL certificates, which enables encrypted
HTTPS for SRS Droplet. It's very easy to use, only by 1-Click.

HTTPS is required for publishing streams using WebRTC, and it improves security. If you want to support the video
streaming in any HTTPS website, such as a WordPress website, you must use HLS/FLV/WebRTC with HTTPS, or it will fail for
security reasons.

> Note that SRS droplet only supports a single domain name, which makes the problem simple. It is easy to use.

In this tutorial, you will learn how to configure the HTTPS for SRS droplets, and your certificate will be renewed automatically.

<!--truncate-->

## Prerequisites

To complete this guide, you will need:

1. An SRS Droplet with Oryx installed, please follow this [set-up a video streaming service](https://blog.ossrs.io/how-to-setup-a-video-streaming-service-by-1-click-e9fe6f314ac6) tutorial.
1. A fully registered domain name, you could purchase a domain name on [Namecheap](https://namecheap.com/) or [GoDaddy](https://godaddy.com/). For the demonstration purpose, however, we will use a placeholder `your_domain_name` throughout this tutorial.

This guide will also use placeholders `your_public_ipv4` and `your_domain_name` throughout. Please replace them with
your own IP address and domain name.

## Step 1 - DNS Records Setup

Make sure setup a DNS record for your server. Please add an `A record` with `your_domain_name` pointing to your server
public IP address, which is `your_public_ipv4` as we mentioned, see [Domains and DNS](https://docs.digitalocean.com/products/networking/dns/how-to/manage-records/#a-records).
Add a record like this:

```text
A your_domain_name your_public_ipv4
```

To check the domain name, your status should look like this:

```bash
ping your_domain_name
```

```text
Output
PING ossrs.io (your_public_ipv4): 56 data bytes
64 bytes from your_public_ipv4: icmp_seq=0 ttl=64 time=11.828 ms
64 bytes from your_public_ipv4: icmp_seq=1 ttl=64 time=16.553 ms
64 bytes from your_public_ipv4: icmp_seq=2 ttl=64 time=12.433 ms
```

If you visit `http://your_domain_name/mgmt`, you should see the Oryx console now.

![](/img/blog-2022-04-12-01.png)

Next, let's fetch our certificates.

## Step 2 - Obtaining an SSL Certificate

Now please switch to `System > HTTPS > Let's Encrypt` and enter `your_domain_name`, then click `Submit` button to request
a free SSL cert from [Let's Encrypt](https://letsencrypt.org/):

![](/img/blog-2022-04-12-02.png)

This runs `certbot` to fetch an SSL certificate. It will communicate with the Let's Encrypt server, then create a challenge
to verify that you control the domain that you're requesting a certificate for. All these are automatically done by Cloud
SRS.

If successful, please try reload your website using `https://your_domain_name/mgmt`. Pay attention to your browser's
security indicator, as demonstrated bellow:

![](/img/blog-2022-04-12-03.png)

> Note: You could request multiple domains separated by semicolon, after adding an A record for each domain, for example,
`domain.com;www.domain.com`, then both `https://domain.com` and `https://www.domain.com` are available.

Let's finish this tutorial by covering the certificate renewal process.

## Step 3 - About Certificate Auto-Renewal

Let's Encrypt's certificates are only valid for about 3 months. Oryx will start a timer to verify if it is due to
renew your certificates on a daily basis, and reload Nginx to apply the changes if neccessary.

You can check the renew log by:

```bash
docker logs platform |grep renew
```

```text
Output
Thread #crontab: auto renew the Let's Encrypt ssl
Thread #crontab: renew ssl updated=false, message is 
Processing /etc/letsencrypt/renewal/lh.ossrs.io.conf
Certificate not yet due for renewal
The following certificates are not due for renewal yet:
No renewals were attempted.
```

If no errors, you're all set.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

In this tutorial, you set-up the DNS A Record, downloaded SSL Certificates for your domain, configured Nginx to apply
the certificate, and set-up automatic renewal.

## Contact

If you have further questions, please contact us by [discord](https://discord.gg/yZ4BnPmHAd).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/22-04-12-Oryx-HTTPS)


