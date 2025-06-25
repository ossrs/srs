---
slug: secure-your-http-api
title: SRS - How to Secure Your HTTP API
authors: []
tags: [api, security, http]
custom_edit_url: null
---

# How to Secure Your HTTP API

After you have built your SRS server, you can use HTTP API to access it by SRS console or other HTTP clients.
However, you should secure your HTTP API to prevent unauthorized access. 
This article describes how to secure your HTTP API.

<!--truncate-->

## Usage

First of all, please upgrade SRS to 5.0.152+ or 6.0.40+, which supports HTTP API security.

Then, please enable the HTTP basic authentication by configuration `http_api.auth`:

```bash
# conf/http.api.auth.conf
http_api {
    enabled on;
    listen 1985;
    auth {
        enabled on;
        username admin;
        password admin;
    }
}
```

Now, start SRS with the config:

```bash
./objs/srs -c conf/http.api.auth.conf
```

Or set up the username and password by environment variables:

```bash
env SRS_HTTP_API_ENABLED=on SRS_HTTP_SERVER_ENABLED=on \
    SRS_HTTP_API_AUTH_ENABLED=on SRS_HTTP_API_AUTH_USERNAME=admin SRS_HTTP_API_AUTH_PASSWORD=admin \
    ./objs/srs -e
```

Now, you can access the following urls to verify it:

* Prompt for username and password: [http://localhost:1985/api/v1/versions](http://localhost:1985/api/v1/versions)
* URL with authentication: [http://admin:admin@localhost:1985/api/v1/versions](http://admin:admin@localhost:1985/api/v1/versions)

To clean up the username and password, you can access the HTTP API with the username only:

* [http://admin@localhost:1985/api/v1/versions](http://admin@localhost:1985/api/v1/versions)

> Note: Please note that we only secure the HTTP API, neither the HTTP server nor the WebRTC HTTP apis.

## SRS Console

SRS console is a web application to manage your SRS server.
When SRS server started, you can access the SRS console by the URL: 
[http://localhost:8080/console/](http://localhost:8080/console/)

If you enable the HTTP API authentication, you should use URL with username and password to access the SRS console:
[http://admin:admin@localhost:8080/console/](http://admin:admin@localhost:8080/console/)

Or, the browser will prompt for username and password.

## For SRS 4.0

SRS 4.0 does not support HTTP API authentication, however there is a workaround to secure your HTTP API.
You can use the nginx to proxy the HTTP API, and enable the HTTP basic authentication by nginx.
Or write a Go HTTP proxy for SRS HTTP API.

```text
Browser ---HTTP-with-Authentication---> Nginx ---HTTP---> SRS 4.0
```

In this solution, you should also change the listen of SRS HTTP API to lo:

```bash
http_api {
    enabled on;
    listen 127.0.0.1:1985;
}
```

So that the HTTP API is only accessible by the localhost proxy server.

## About WebRTC API Security

SRS doesn't support HTTP API authentication for WebRTC,
instead, you should use the HTTP callback to verify the client.

Please see [WebRTC HTTP Callback](../docs/v5/doc/http-callback) and [Token Authentication](../docs/v5/doc/drm#token-authentication) for more details.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

GitHub Copilot and I wrote this article.

The code of this feature was written by SRS developers and GitHub Copilot.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/23-04-02-Secure-Your-HTTP-API)
