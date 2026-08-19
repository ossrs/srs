---
slug: BT-aaPanel
title: Oryx - How to Setup a Video Streaming Service with aaPanel
authors: []
tags: [tutorial, bt, aapanel, streaming]
custom_edit_url: null
---

# How to Setup a Video Streaming Service with aaPanel

## Introduction

[aaPanel](https://www.aapanel.com) is a simple website and server management tool. It is similar to [cpanel.net](https://cpanel.net/),
while aaPanel is free, open source and easy to develop plugins for media servers.

In this tutorial, you will learn how to deploy a live streaming media server, using aaPanel. If you have websites
deployed with aaPanel, it's also possible to deploy an extra media server to power your website with live streaming
service, for example, to enable live streaming feature for your WordPress website.

<!--truncate-->

## Prerequisites

To complete this guide, you should have:

1. A linux server, with aaPanel installed. Please use a CentOS 7+ or Ubuntu 20+ and install aaPanel on it by using [command here](https://www.aapanel.com/install.html).
2. **NGINX** based framework, if you also need to install websites. [Oryx](https://github.com/ossrs/oryx) requires **NGINX** to proxy to our backend services.
3. A DNS domain name, if you want to use HTTPS.

## Step 1: Install aaPanel

If you don't have aaPanel installed, please follow this [tutorial](https://www.aapanel.com/install.html). Highly
recommend install aaPanel on Ubuntu 20+:

```bash
wget -O install.sh https://download.bt.cn/install/install-ubuntu_6.0.sh && sudo bash install.sh ed8484bec
```

After installation, you will get a url, username and password for login, for example:

```text
==================================================================
Congratulations! Installed successfully!
==================================================================
aaPanel Internet Address: http://159.223.85.157:7800/a954fbf9
username: f6nka6v8
password: 4b43d253
```

Visit the url in a browser, as demonstrated below:

![](/img/blog-2022-04-29-en-001.png)

You might develop your website with WordPress, however it's optional. In the next step, let's set-up a media server with
aaPanel for live streaming.

## Step 2: Install Oryx

There are two ways to install Oryx. If you find the `Oryx` plugin in App Store, you could install it from
there, as shown in the following picture:

![](/img/blog-2022-04-29-en-002.png)

If not found in App Store, you could also download the latest version of the plugin in the oryx repository on
Github: [aapanel-oryx.zip](https://github.com/ossrs/oryx/releases/latest/download/aapanel-oryx.zip). Then
you could upload the zip file and install the plugin, as demonstrated below:

![](/img/blog-2022-04-29-en-003.png)

Both approaches will lead you to a installation guide, like this:

![](/img/blog-2022-04-29-en-004.png)

Please click on `Confirm to install` to continue the plugin installation. In the next step, we will set-up Oryx and
install some dependencies.

## Step 3: Setup Oryx

To set-up `Oryx`, please click the `Setting` button under `Operation` field:

![](/img/blog-2022-04-29-en-005.png)

Then click on `Install Oryx` to install the required softwares:

![](/img/blog-2022-04-29-en-006.png)

Oryx requires NGINX, Nodejs and Docker, please install all of them:

![](/img/blog-2022-04-29-en-007.png)

After installing all dependencies, continue to install Oryx:

![](/img/blog-2022-04-29-en-008.png)

When completion all the set-up, click on `Dashboard` and then visit the `Oryx Dashboard` via the link on the page:

![](/img/blog-2022-04-29-en-009.png)

Then set-up the admin password:

![](/img/blog-2022-04-29-en-010.png)

You could follow the tutorials to use Oryx:

![](/img/blog-2022-04-29-en-011.png)

Now we have a media server. In the next step, we publish a live stream to the server and play it through WordPress.

## Step 4: Publish and Play Live Stream

Please open the instruction page here: `Scenario > Streaming > OBS or vMix`

![](/img/blog-2022-04-29-en-012.png)

We recommend using OBS. You can download OBS from [here](https://obsproject.com/download), and configure it from
`Settings > Stream`:

![](/img/blog-2022-04-29-en-013.png)

Then, click on the `Start Streaming` to publish a live stream to your server. You could play the HTTP-FLV, HLS or WebRTC
either use a browser, or through WordPress:

```text
[srs_player url="http://159.223.85.157/live/livestream.m3u8"]
```

![](/img/blog-2022-04-29-en-014.png)

For details about WordPress shortcodes, please read this tutorial: [how to publish your SRS livestream through WordPress](https://blog.ossrs.io/publish-your-srs-livestream-through-wordpress-ec18dfae7d6f).
There is also a live demonstration here [SRS Player Demo](https://wp.ossrs.io/2022/04/25/srs-player/).

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

In this tutorial, you create a live streaming service with aaPanel, publish a stream with OBS and then play it through a
browser or WordPress. If you have further questions about SRS, [the wiki](../docs/v4/doc/introduction)
is a good place to start.

## Contact

If you'd like to discuss with SRS community members, you are welcome to join us on [discord](https://discord.gg/yZ4BnPmHAd).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/22-04-29-BT-aaPanel)


