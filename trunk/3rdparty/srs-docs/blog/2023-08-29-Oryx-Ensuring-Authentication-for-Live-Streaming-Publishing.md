---
slug: Ensuring-Authentication-in-Live-Streaming-Publishing
title: Oryx - Ensuring Authentication in Live Streaming Publishing
authors: []
tags: [live streaming, security, authentication]
custom_edit_url: null
---

# Ensuring Authentication in Live Streaming Publishing

In today's digital age, live streaming has become increasingly popular, with platforms like YouTube and 
Twitch offering users the ability to broadcast their content in real-time. However, with this growing 
popularity comes the need for enhanced security and authentication measures to protect both streamers 
and viewers. In this comprehensive guide, we will delve into the importance of security and authentication 
in live streaming, discuss the Oryx solution for secure publishing, and provide a step-by-step guide 
on setting up the Oryx for your own live streaming service.

<!--truncate-->

## The Importance of Authentication in Live Streaming

When publishing a live stream on platforms like YouTube or Twitch, users need to obtain a stream key 
and use broadcasting software like OBS to publish the live stream to the platform. Maintaining the 
confidentiality of your stream key is essential, as anyone who obtains it can stream on your behalf, 
potentially publishing prohibited content and resulting in your account being blocked by the platform.

For those looking to build their own live streaming service, ensuring security and authentication 
becomes even more critical. This is where the Oryx comes in, offering a solution for secure 
publishing through the use of a publish secret.

However, an overly complex security design can also lead to confusion and difficulty for users, 
making the streaming platform challenging to use. Therefore, it is crucial to implement a secure 
yet straightforward solution for authenticating live streaming publishing.

## The Oryx Solution for Secure Publishing

The Oryx is a powerful tool that helps users build their own live streaming service with enhanced 
security and authentication features. By utilizing a publish secret, the Oryx ensures that only 
authorized users can publish streams, preventing unauthorized access and protecting your content.

To set up the Oryx for secure publishing, follow these steps:

1. Create an Oryx with just one click. For detailed instructions, visit [How to Setup a Video Streaming Service by 1-Click](./2022-04-09-Oryx-Tutorial.md)

1. Upon creation, the Oryx will automatically generate a publish secret, such as `5181a08ee6eab86597e913e1f9e4c294`. You can set it from `System / Authentication / Update Stream Secret`.

1. Copy the OBS settings from `Scenarios / Streaming`, including the Server and StreamKey. For example, the Server might be `rtmp://135.98.31.15/live/`, and the StreamKey could be `livestream?secret=5181a08ee6eab86597e913e1f9e4c294`.

1. Open the HLS player to play the stream, such as `http://135.98.31.15/live/livestream.m3u8`. 

For example, you will get the following URLs:
* Publish URL: `rtmp://135.98.31.15/live/livestream?secret=5181a08ee6eab86597e913e1f9e4c294`
* Play URL: `http://135.98.31.15/live/livestream.m3u8`

Note that the secret is not included in the play stream URL, preventing viewers from knowing the publish 
secret and ensuring they can only watch the stream, not publish it.

## Supporting Multiple Streams

The Oryx allows users to support multiple streams by simply changing the resource name (stream name) as 
needed. For example, change stream name from `livestream` to `movie`:
- Publish URL: `rtmp://135.98.31.15/live/movie?secret=5181a08ee6eab86597e913e1f9e4c294`
- Play URL: `http://135.98.31.15/live/movie.m3u8`

Or you can change `livestream` to `sport`:
- Publish URL: `rtmp://135.98.31.15/live/sport?secret=5181a08ee6eab86597e913e1f9e4c294`
- Play URL: `http://135.98.31.15/live/sport.m3u8`

You can use any name you prefer, for example, `the-10-years-anually-for-you`:
- Publish URL: `rtmp://135.98.31.15/live/the-10-years-anually-for-you?secret=5181a08ee6eab86597e913e1f9e4c294`
- Play URL: `http://135.98.31.15/live/the-10-years-anually-for-you.m3u8`

> Note: Please note that all streams share the same publish secret.

The Oryx allows for an unlimited number of simultaneous streams, with the only constraint being your 
server's bandwidth capacity. Additionally, you have the freedom to choose any desired stream name.

## Limitations of the Oryx

Nevertheless, the Oryx has a few limitations. 

As of now, it only supports authentication for publishing streams using a global secret applicable to 
all streams. It does not provide individual secrets for each stream in an effort to maintain simplicity.

Additionally, unlike YouTube and Twitch, the Oryx does not support changing stream names, which use 
different names for publishing and playing. This can be confusing and complex, as it requires an additional 
system to manage the mapping of filenames.

Finally, the Oryx does not have authentication support for playing streams at the moment, making the 
stream view public and accessible to anyone. If you would like to limit the playback of the stream, please 
let us know. This feature is included in the milestone, but there is no specific timeline for its 
implementation.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

In summary, security and authentication are vital aspects of live streaming, and the Oryx provides a 
straightforward solution for those building their own streaming services. By following the steps outlined 
in this guide, you can ensure that your live streaming service remains secure and accessible only to 
authorized users.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/2023-08-29-Ensuring-Authentication-for-Live-Streaming-Publishing)
