
/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2021 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

'use strict';

// Depends on adapter-7.4.0.min.js from https://github.com/webrtc/adapter
// Async-awat-prmise based SRS RTC Publisher.
function SrsRtcPublisherAsync() {
    var self = {};

    // @see https://github.com/rtcdn/rtcdn-draft
    // @url The WebRTC url to play with, for example:
    //      webrtc://r.ossrs.net/live/livestream
    // or specifies the API port:
    //      webrtc://r.ossrs.net:11985/live/livestream
    // or autostart the publish:
    //      webrtc://r.ossrs.net/live/livestream?autostart=true
    // or change the app from live to myapp:
    //      webrtc://r.ossrs.net:11985/myapp/livestream
    // or change the stream from livestream to mystream:
    //      webrtc://r.ossrs.net:11985/live/mystream
    // or set the api server to myapi.domain.com:
    //      webrtc://myapi.domain.com/live/livestream
    // or set the candidate(ip) of answer:
    //      webrtc://r.ossrs.net/live/livestream?eip=39.107.238.185
    // or force to access https API:
    //      webrtc://r.ossrs.net/live/livestream?schema=https
    // or use plaintext, without SRTP:
    //      webrtc://r.ossrs.net/live/livestream?encrypt=false
    // or any other information, will pass-by in the query:
    //      webrtc://r.ossrs.net/live/livestream?vhost=xxx
    //      webrtc://r.ossrs.net/live/livestream?token=xxx
    self.publish = async function (url) {
        var conf = self.__internal.prepareUrl(url);
        self.pc.addTransceiver("audio", {direction: "sendonly"});
        self.pc.addTransceiver("video", {direction: "sendonly"});

        var stream = await navigator.mediaDevices.getUserMedia(
            {audio: true, video: {width: {max: 320}}}
        );
        // @see https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/addStream#Migrating_to_addTrack
        stream.getTracks().forEach(function (track) {
            self.pc.addTrack(track);
        });

        var offer = await self.pc.createOffer();
        await self.pc.setLocalDescription(offer);
        var session = await new Promise(function (resolve, reject) {
            // @see https://github.com/rtcdn/rtcdn-draft
            var data = {
                api: conf.apiUrl, tid: conf.tid, streamurl: conf.streamUrl,
                clientip: null, sdp: offer.sdp
            };
            console.log("Generated offer: ", data);

            $.ajax({
                type: "POST", url: conf.apiUrl, data: JSON.stringify(data),
                contentType: 'application/json', dataType: 'json'
            }).done(function (data) {
                console.log("Got answer: ", data);
                if (data.code) {
                    reject(data);
                    return;
                }

                resolve(data);
            }).fail(function (reason) {
                reject(reason);
            });
        });
        await self.pc.setRemoteDescription(
            new RTCSessionDescription({type: 'answer', sdp: session.sdp})
        );
        session.simulator = conf.schema + '//' + conf.urlObject.server + ':' + conf.port + '/rtc/v1/nack/';

        // Notify about local stream when success.
        self.onaddstream && self.onaddstream({stream: stream});

        return session;
    };

    // Close the publisher.
    self.close = function () {
        self.pc && self.pc.close();
        self.pc = null;
    };

    // The callback when got local stream.
    self.onaddstream = function (event) {
    };

    // Internal APIs.
    self.__internal = {
        defaultPath: '/rtc/v1/publish/',
        prepareUrl: function (webrtcUrl) {
            var urlObject = self.__internal.parse(webrtcUrl);

            // If user specifies the schema, use it as API schema.
            var schema = urlObject.user_query.schema;
            schema = schema ? schema + ':' : window.location.protocol;

            var port = urlObject.port || 1985;
            if (schema === 'https:') {
                port = urlObject.port || 443;
            }

            // @see https://github.com/rtcdn/rtcdn-draft
            var api = urlObject.user_query.play || self.__internal.defaultPath;
            if (api.lastIndexOf('/') !== api.length - 1) {
                api += '/';
            }

            apiUrl = schema + '//' + urlObject.server + ':' + port + api;
            for (var key in urlObject.user_query) {
                if (key !== 'api' && key !== 'play') {
                    apiUrl += '&' + key + '=' + urlObject.user_query[key];
                }
            }
            // Replace /rtc/v1/play/&k=v to /rtc/v1/play/?k=v
            var apiUrl = apiUrl.replace(api + '&', api + '?');

            var streamUrl = urlObject.url;

            return {
                apiUrl: apiUrl, streamUrl: streamUrl, schema: schema, urlObject: urlObject, port: port,
                tid: Number(new Date().getTime() + parseInt(String(Math.random() * 10000000000))).toString(16)
            };
        },
        parse: function (url) {
            // @see: http://stackoverflow.com/questions/10469575/how-to-use-location-object-to-parse-url-without-redirecting-the-page-in-javascri
            var a = document.createElement("a");
            a.href = url.replace("rtmp://", "http://")
                .replace("webrtc://", "http://")
                .replace("rtc://", "http://");

            var vhost = a.hostname;
            var app = a.pathname.substr(1, a.pathname.lastIndexOf("/") - 1);
            var stream = a.pathname.substr(a.pathname.lastIndexOf("/") + 1);

            // parse the vhost in the params of app, that srs supports.
            app = app.replace("...vhost...", "?vhost=");
            if (app.indexOf("?") >= 0) {
                var params = app.substr(app.indexOf("?"));
                app = app.substr(0, app.indexOf("?"));

                if (params.indexOf("vhost=") > 0) {
                    vhost = params.substr(params.indexOf("vhost=") + "vhost=".length);
                    if (vhost.indexOf("&") > 0) {
                        vhost = vhost.substr(0, vhost.indexOf("&"));
                    }
                }
            }

            // when vhost equals to server, and server is ip,
            // the vhost is __defaultVhost__
            if (a.hostname === vhost) {
                var re = /^(\d+)\.(\d+)\.(\d+)\.(\d+)$/;
                if (re.test(a.hostname)) {
                    vhost = "__defaultVhost__";
                }
            }

            // parse the schema
            var schema = "rtmp";
            if (url.indexOf("://") > 0) {
                schema = url.substr(0, url.indexOf("://"));
            }

            var port = a.port;
            if (!port) {
                if (schema === 'http') {
                    port = 80;
                } else if (schema === 'https') {
                    port = 443;
                } else if (schema === 'rtmp') {
                    port = 1935;
                }
            }

            var ret = {
                url: url,
                schema: schema,
                server: a.hostname, port: port,
                vhost: vhost, app: app, stream: stream
            };
            self.__internal.fill_query(a.search, ret);

            // For webrtc API, we use 443 if page is https, or schema specified it.
            if (!ret.port) {
                if (schema === 'webrtc' || schema === 'rtc') {
                    if (ret.user_query.schema === 'https') {
                        ret.port = 443;
                    } else if (window.location.href.indexOf('https://') === 0) {
                        ret.port = 443;
                    } else {
                        // For WebRTC, SRS use 1985 as default API port.
                        ret.port = 1985;
                    }
                }
            }

            return ret;
        },
        fill_query: function (query_string, obj) {
            // pure user query object.
            obj.user_query = {};

            if (query_string.length === 0) {
                return;
            }

            // split again for angularjs.
            if (query_string.indexOf("?") >= 0) {
                query_string = query_string.split("?")[1];
            }

            var queries = query_string.split("&");
            for (var i = 0; i < queries.length; i++) {
                var elem = queries[i];

                var query = elem.split("=");
                obj[query[0]] = query[1];
                obj.user_query[query[0]] = query[1];
            }

            // alias domain for vhost.
            if (obj.domain) {
                obj.vhost = obj.domain;
            }
        }
    };

    self.pc = new RTCPeerConnection(null);

    return self;
}

// Depends on adapter-7.4.0.min.js from https://github.com/webrtc/adapter
// Async-await-promise based SRS RTC Player.
function SrsRtcPlayerAsync() {
    var self = {};

    // @see https://github.com/rtcdn/rtcdn-draft
    // @url The WebRTC url to play with, for example:
    //      webrtc://r.ossrs.net/live/livestream
    // or specifies the API port:
    //      webrtc://r.ossrs.net:11985/live/livestream
    // or autostart the play:
    //      webrtc://r.ossrs.net/live/livestream?autostart=true
    // or change the app from live to myapp:
    //      webrtc://r.ossrs.net:11985/myapp/livestream
    // or change the stream from livestream to mystream:
    //      webrtc://r.ossrs.net:11985/live/mystream
    // or set the api server to myapi.domain.com:
    //      webrtc://myapi.domain.com/live/livestream
    // or set the candidate(ip) of answer:
    //      webrtc://r.ossrs.net/live/livestream?eip=39.107.238.185
    // or force to access https API:
    //      webrtc://r.ossrs.net/live/livestream?schema=https
    // or use plaintext, without SRTP:
    //      webrtc://r.ossrs.net/live/livestream?encrypt=false
    // or any other information, will pass-by in the query:
    //      webrtc://r.ossrs.net/live/livestream?vhost=xxx
    //      webrtc://r.ossrs.net/live/livestream?token=xxx
    self.play = async function(url) {
        var conf = self.__internal.prepareUrl(url);
        self.pc.addTransceiver("audio", {direction: "recvonly"});
        self.pc.addTransceiver("video", {direction: "recvonly"});

        var offer = await self.pc.createOffer();
        await self.pc.setLocalDescription(offer);
        var session = await new Promise(function(resolve, reject) {
            // @see https://github.com/rtcdn/rtcdn-draft
            var data = {
                api: conf.apiUrl, tid: conf.tid, streamurl: conf.streamUrl,
                clientip: null, sdp: offer.sdp
            };
            console.log("Generated offer: ", data);

            $.ajax({
                type: "POST", url: conf.apiUrl, data: JSON.stringify(data),
                contentType:'application/json', dataType: 'json'
            }).done(function(data) {
                console.log("Got answer: ", data);
                if (data.code) {
                    reject(data); return;
                }

                resolve(data);
            }).fail(function(reason){
                reject(reason);
            });
        });
        await self.pc.setRemoteDescription(
            new RTCSessionDescription({type: 'answer', sdp: session.sdp})
        );
        return session;
    };

    // Close the player.
    self.close = function() {
        self.pc && self.pc.close();
        self.pc = null;
    };

    // The callback when got remote stream.
    self.onaddstream = function (event) {};

    // Internal APIs.
    self.__internal = {
        defaultPath: '/rtc/v1/play/',
        prepareUrl: function (webrtcUrl) {
            var urlObject = self.__internal.parse(webrtcUrl);

            // If user specifies the schema, use it as API schema.
            var schema = urlObject.user_query.schema;
            schema = schema ? schema + ':' : window.location.protocol;

            var port = urlObject.port || 1985;
            if (schema === 'https:') {
                port = urlObject.port || 443;
            }

            // @see https://github.com/rtcdn/rtcdn-draft
            var api = urlObject.user_query.play || self.__internal.defaultPath;
            if (api.lastIndexOf('/') !== api.length - 1) {
                api += '/';
            }

            apiUrl = schema + '//' + urlObject.server + ':' + port + api;
            for (var key in urlObject.user_query) {
                if (key !== 'api' && key !== 'play') {
                    apiUrl += '&' + key + '=' + urlObject.user_query[key];
                }
            }
            // Replace /rtc/v1/play/&k=v to /rtc/v1/play/?k=v
            var apiUrl = apiUrl.replace(api + '&', api + '?');

            var streamUrl = urlObject.url;

            return {
                apiUrl: apiUrl, streamUrl: streamUrl, schema: schema, urlObject: urlObject, port: port,
                tid: Number(new Date().getTime() + parseInt(String(Math.random() * 10000000000))).toString(16)
            };
        },
        parse: function (url) {
            // @see: http://stackoverflow.com/questions/10469575/how-to-use-location-object-to-parse-url-without-redirecting-the-page-in-javascri
            var a = document.createElement("a");
            a.href = url.replace("rtmp://", "http://")
                .replace("webrtc://", "http://")
                .replace("rtc://", "http://");

            var vhost = a.hostname;
            var app = a.pathname.substr(1, a.pathname.lastIndexOf("/") - 1);
            var stream = a.pathname.substr(a.pathname.lastIndexOf("/") + 1);

            // parse the vhost in the params of app, that srs supports.
            app = app.replace("...vhost...", "?vhost=");
            if (app.indexOf("?") >= 0) {
                var params = app.substr(app.indexOf("?"));
                app = app.substr(0, app.indexOf("?"));

                if (params.indexOf("vhost=") > 0) {
                    vhost = params.substr(params.indexOf("vhost=") + "vhost=".length);
                    if (vhost.indexOf("&") > 0) {
                        vhost = vhost.substr(0, vhost.indexOf("&"));
                    }
                }
            }

            // when vhost equals to server, and server is ip,
            // the vhost is __defaultVhost__
            if (a.hostname === vhost) {
                var re = /^(\d+)\.(\d+)\.(\d+)\.(\d+)$/;
                if (re.test(a.hostname)) {
                    vhost = "__defaultVhost__";
                }
            }

            // parse the schema
            var schema = "rtmp";
            if (url.indexOf("://") > 0) {
                schema = url.substr(0, url.indexOf("://"));
            }

            var port = a.port;
            if (!port) {
                if (schema === 'http') {
                    port = 80;
                } else if (schema === 'https') {
                    port = 443;
                } else if (schema === 'rtmp') {
                    port = 1935;
                }
            }

            var ret = {
                url: url,
                schema: schema,
                server: a.hostname, port: port,
                vhost: vhost, app: app, stream: stream
            };
            self.__internal.fill_query(a.search, ret);

            // For webrtc API, we use 443 if page is https, or schema specified it.
            if (!ret.port) {
                if (schema === 'webrtc' || schema === 'rtc') {
                    if (ret.user_query.schema === 'https') {
                        ret.port = 443;
                    } else if (window.location.href.indexOf('https://') === 0) {
                        ret.port = 443;
                    } else {
                        // For WebRTC, SRS use 1985 as default API port.
                        ret.port = 1985;
                    }
                }
            }

            return ret;
        },
        fill_query: function (query_string, obj) {
            // pure user query object.
            obj.user_query = {};

            if (query_string.length === 0) {
                return;
            }

            // split again for angularjs.
            if (query_string.indexOf("?") >= 0) {
                query_string = query_string.split("?")[1];
            }

            var queries = query_string.split("&");
            for (var i = 0; i < queries.length; i++) {
                var elem = queries[i];

                var query = elem.split("=");
                obj[query[0]] = query[1];
                obj.user_query[query[0]] = query[1];
            }

            // alias domain for vhost.
            if (obj.domain) {
                obj.vhost = obj.domain;
            }
        }
    };

    self.pc = new RTCPeerConnection(null);
    self.pc.onaddstream = function (event) {
        if (self.onaddstream) {
            self.onaddstream(event);
        }
    };

    return self;
}

// Format the codec of RTCRtpSender, kind(audio/video) is optional filter.
// https://developer.mozilla.org/en-US/docs/Web/Media/Formats/WebRTC_codecs#getting_the_supported_codecs
function SrsRtcFormatSenders(senders, kind) {
    var codecs = [];
    senders.forEach(function (sender) {
        sender.getParameters().codecs.forEach(function(c) {
            if (kind && sender.track.kind !== kind) {
                return;
            }

            if (c.mimeType.indexOf('/red') > 0 || c.mimeType.indexOf('/rtx') > 0 || c.mimeType.indexOf('/fec') > 0) {
                return;
            }

            var s = '';

            s += c.mimeType.replace('audio/', '').replace('video/', '');
            s += ', ' + c.clockRate + 'HZ';
            if (sender.track.kind === "audio") {
                s += ', channels: ' + c.channels;
            }
            s += ', pt: ' + c.payloadType;

            codecs.push(s);
        });
    });
    return codecs.join(", ");
}

