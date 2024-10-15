
//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

'use strict';

function SrsError(name, message) {
    this.name = name;
    this.message = message;
    this.stack = (new Error()).stack;
}
SrsError.prototype = Object.create(Error.prototype);
SrsError.prototype.constructor = SrsError;

// Depends on adapter-7.4.0.min.js from https://github.com/webrtc/adapter
// Async-awat-prmise based SRS RTC Publisher.
function SrsRtcPublisherAsync() {
    var self = {};

    // https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/getUserMedia
    self.constraints = {
        audio: true,
        video: {
            width: {ideal: 320, max: 576}
        }
    };

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
    // or set the candidate(eip) of answer:
    //      webrtc://r.ossrs.net/live/livestream?candidate=39.107.238.185
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
        //self.pc.addTransceiver("video", {direction: "sendonly"});
        //self.pc.addTransceiver("audio", {direction: "sendonly"});

        if (!navigator.mediaDevices && window.location.protocol === 'http:' && window.location.hostname !== 'localhost') {
            throw new SrsError('HttpsRequiredError', `Please use HTTPS or localhost to publish, read https://github.com/ossrs/srs/issues/2762#issuecomment-983147576`);
        }
        var stream = await navigator.mediaDevices.getUserMedia(self.constraints);

        // @see https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/addStream#Migrating_to_addTrack
        stream.getTracks().forEach(function (track) {
            self.pc.addTrack(track);

            // Notify about local track when stream is ok.
            self.ontrack && self.ontrack({track: track});
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

            const xhr = new XMLHttpRequest();
            xhr.onload = function() {
                if (xhr.readyState !== xhr.DONE) return;
                if (xhr.status !== 200 && xhr.status !== 201) return reject(xhr);
                const data = JSON.parse(xhr.responseText);
                console.log("Got answer: ", data);
                return data.code ? reject(xhr) : resolve(data);
            }
            xhr.open('POST', conf.apiUrl, true);
            xhr.setRequestHeader('Content-type', 'application/json');
            xhr.send(JSON.stringify(data));
        });
        await self.pc.setRemoteDescription(
            new RTCSessionDescription({type: 'answer', sdp: session.sdp})
        );
        session.simulator = conf.schema + '//' + conf.urlObject.server + ':' + conf.port + '/rtc/v1/nack/';

        return session;
    };

    // Close the publisher.
    self.close = function () {
        self.pc && self.pc.close();
        self.pc = null;
    };

    // The callback when got local stream.
    // @see https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/addStream#Migrating_to_addTrack
    self.ontrack = function (event) {
        // Add track to stream of SDK.
        self.stream.addTrack(event.track);
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

            var apiUrl = schema + '//' + urlObject.server + ':' + port + api;
            for (var key in urlObject.user_query) {
                if (key !== 'api' && key !== 'play') {
                    apiUrl += '&' + key + '=' + urlObject.user_query[key];
                }
            }
            // Replace /rtc/v1/play/&k=v to /rtc/v1/play/?k=v
            apiUrl = apiUrl.replace(api + '&', api + '?');

            var streamUrl = urlObject.url;

            return {
                apiUrl: apiUrl, streamUrl: streamUrl, schema: schema, urlObject: urlObject, port: port,
                tid: Number(parseInt(new Date().getTime()*Math.random()*100)).toString(16).slice(0, 7)
            };
        },
        parse: function (url) {
            // @see: http://stackoverflow.com/questions/10469575/how-to-use-location-object-to-parse-url-without-redirecting-the-page-in-javascri
            var a = document.createElement("a");
            a.href = url.replace("rtmp://", "http://")
                .replace("webrtc://", "http://")
                .replace("rtc://", "http://");

            var vhost = a.hostname;
            var app = a.pathname.substring(1, a.pathname.lastIndexOf("/"));
            var stream = a.pathname.slice(a.pathname.lastIndexOf("/") + 1);

            // parse the vhost in the params of app, that srs supports.
            app = app.replace("...vhost...", "?vhost=");
            if (app.indexOf("?") >= 0) {
                var params = app.slice(app.indexOf("?"));
                app = app.slice(0, app.indexOf("?"));

                if (params.indexOf("vhost=") > 0) {
                    vhost = params.slice(params.indexOf("vhost=") + "vhost=".length);
                    if (vhost.indexOf("&") > 0) {
                        vhost = vhost.slice(0, vhost.indexOf("&"));
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
                schema = url.slice(0, url.indexOf("://"));
            }

            var port = a.port;
            if (!port) {
                // Finger out by webrtc url, if contains http or https port, to overwrite default 1985.
                if (schema === 'webrtc' && url.indexOf(`webrtc://${a.host}:`) === 0) {
                    port = (url.indexOf(`webrtc://${a.host}:80`) === 0) ? 80 : 443;
                }

                // Guess by schema.
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

    // To keep api consistent between player and publisher.
    // @see https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/addStream#Migrating_to_addTrack
    // @see https://webrtc.org/getting-started/media-devices
    self.stream = new MediaStream();

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
    //      webrtc://r.ossrs.net:80/live/livestream
    // or autostart the play:
    //      webrtc://r.ossrs.net/live/livestream?autostart=true
    // or change the app from live to myapp:
    //      webrtc://r.ossrs.net:11985/myapp/livestream
    // or change the stream from livestream to mystream:
    //      webrtc://r.ossrs.net:11985/live/mystream
    // or set the api server to myapi.domain.com:
    //      webrtc://myapi.domain.com/live/livestream
    // or set the candidate(eip) of answer:
    //      webrtc://r.ossrs.net/live/livestream?candidate=39.107.238.185
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
        //self.pc.addTransceiver("video", {direction: "recvonly"});
        //self.pc.addTransceiver("audio", {direction: "recvonly"});

        var offer = await self.pc.createOffer();
        await self.pc.setLocalDescription(offer);
        var session = await new Promise(function(resolve, reject) {
            // @see https://github.com/rtcdn/rtcdn-draft
            var data = {
                api: conf.apiUrl, tid: conf.tid, streamurl: conf.streamUrl,
                clientip: null, sdp: offer.sdp
            };
            console.log("Generated offer: ", data);

            const xhr = new XMLHttpRequest();
            xhr.onload = function() {
                if (xhr.readyState !== xhr.DONE) return;
                if (xhr.status !== 200 && xhr.status !== 201) return reject(xhr);
                const data = JSON.parse(xhr.responseText);
                console.log("Got answer: ", data);
                return data.code ? reject(xhr) : resolve(data);
            }
            xhr.open('POST', conf.apiUrl, true);
            xhr.setRequestHeader('Content-type', 'application/json');
            xhr.send(JSON.stringify(data));
        });
        await self.pc.setRemoteDescription(
            new RTCSessionDescription({type: 'answer', sdp: session.sdp})
        );
        session.simulator = conf.schema + '//' + conf.urlObject.server + ':' + conf.port + '/rtc/v1/nack/';

        return session;
    };

    // Close the player.
    self.close = function() {
        self.pc && self.pc.close();
        self.pc = null;
    };

    // The callback when got remote track.
    // Note that the onaddstream is deprecated, @see https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/onaddstream
    self.ontrack = function (event) {
        // https://webrtc.org/getting-started/remote-streams
        self.stream.addTrack(event.track);
    };

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

            var apiUrl = schema + '//' + urlObject.server + ':' + port + api;
            for (var key in urlObject.user_query) {
                if (key !== 'api' && key !== 'play') {
                    apiUrl += '&' + key + '=' + urlObject.user_query[key];
                }
            }
            // Replace /rtc/v1/play/&k=v to /rtc/v1/play/?k=v
            apiUrl = apiUrl.replace(api + '&', api + '?');

            var streamUrl = urlObject.url;

            return {
                apiUrl: apiUrl, streamUrl: streamUrl, schema: schema, urlObject: urlObject, port: port,
                tid: Number(parseInt(new Date().getTime()*Math.random()*100)).toString(16).slice(0, 7)
            };
        },
        parse: function (url) {
            // @see: http://stackoverflow.com/questions/10469575/how-to-use-location-object-to-parse-url-without-redirecting-the-page-in-javascri
            var a = document.createElement("a");
            a.href = url.replace("rtmp://", "http://")
                .replace("webrtc://", "http://")
                .replace("rtc://", "http://");

            var vhost = a.hostname;
            var app = a.pathname.substring(1, a.pathname.lastIndexOf("/"));
            var stream = a.pathname.slice(a.pathname.lastIndexOf("/") + 1);

            // parse the vhost in the params of app, that srs supports.
            app = app.replace("...vhost...", "?vhost=");
            if (app.indexOf("?") >= 0) {
                var params = app.slice(app.indexOf("?"));
                app = app.slice(0, app.indexOf("?"));

                if (params.indexOf("vhost=") > 0) {
                    vhost = params.slice(params.indexOf("vhost=") + "vhost=".length);
                    if (vhost.indexOf("&") > 0) {
                        vhost = vhost.slice(0, vhost.indexOf("&"));
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
                schema = url.slice(0, url.indexOf("://"));
            }

            var port = a.port;
            if (!port) {
                // Finger out by webrtc url, if contains http or https port, to overwrite default 1985.
                if (schema === 'webrtc' && url.indexOf(`webrtc://${a.host}:`) === 0) {
                    port = (url.indexOf(`webrtc://${a.host}:80`) === 0) ? 80 : 443;
                }

                // Guess by schema.
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

    // Create a stream to add track to the stream, @see https://webrtc.org/getting-started/remote-streams
    self.stream = new MediaStream();

    // https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/ontrack
    self.pc.ontrack = function(event) {
        if (self.ontrack) {
            self.ontrack(event);
        }
    };

    return self;
}

// Depends on adapter-7.4.0.min.js from https://github.com/webrtc/adapter
// Async-awat-prmise based SRS RTC Publisher by WHIP.
function SrsRtcWhipWhepAsync() {
    var self = {};

    // https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/getUserMedia
    self.constraints = {
        audio: true,
        video: {
            width: {ideal: 320, max: 576}
        }
    };

    // See https://datatracker.ietf.org/doc/draft-ietf-wish-whip/
    // @url The WebRTC url to publish with, for example:
    //      http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream
    // @options The options to control playing, supports:
    //      videoOnly: boolean, whether only play video, default to false.
    //      audioOnly: boolean, whether only play audio, default to false.
    self.publish = async function (url, options) {
        if (url.indexOf('/whip/') === -1) throw new Error(`invalid WHIP url ${url}`);
        if (options?.videoOnly && options?.audioOnly) throw new Error(`The videoOnly and audioOnly in options can't be true at the same time`);

        if (!options?.videoOnly) {
            self.pc.addTransceiver("audio", {direction: "sendonly"});
        } else {
            self.constraints.audio = false;
        }

        if (!options?.audioOnly) {
            self.pc.addTransceiver("video", {direction: "sendonly"});
        } else {
            self.constraints.video = false;
        }

        if (!navigator.mediaDevices && window.location.protocol === 'http:' && window.location.hostname !== 'localhost') {
            throw new SrsError('HttpsRequiredError', `Please use HTTPS or localhost to publish, read https://github.com/ossrs/srs/issues/2762#issuecomment-983147576`);
        }
        var stream = await navigator.mediaDevices.getUserMedia(self.constraints);

        // @see https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/addStream#Migrating_to_addTrack
        stream.getTracks().forEach(function (track) {
            self.pc.addTrack(track);

            // Notify about local track when stream is ok.
            self.ontrack && self.ontrack({track: track});
        });

        var offer = await self.pc.createOffer();
        await self.pc.setLocalDescription(offer);
        const answer = await new Promise(function (resolve, reject) {
            console.log(`Generated offer: ${offer.sdp}`);

            const xhr = new XMLHttpRequest();
            xhr.onload = function() {
                if (xhr.readyState !== xhr.DONE) return;
                if (xhr.status !== 200 && xhr.status !== 201) return reject(xhr);
                const data = xhr.responseText;
                console.log("Got answer: ", data);
                return data.code ? reject(xhr) : resolve(data);
            }
            xhr.open('POST', url, true);
            xhr.setRequestHeader('Content-type', 'application/sdp');
            xhr.send(offer.sdp);
        });
        await self.pc.setRemoteDescription(
            new RTCSessionDescription({type: 'answer', sdp: answer})
        );

        return self.__internal.parseId(url, offer.sdp, answer);
    };

    // See https://datatracker.ietf.org/doc/draft-ietf-wish-whip/
    // @url The WebRTC url to play with, for example:
    //      http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream
    // @options The options to control playing, supports:
    //      videoOnly: boolean, whether only play video, default to false.
    //      audioOnly: boolean, whether only play audio, default to false.
    self.play = async function(url, options) {
        if (url.indexOf('/whip-play/') === -1 && url.indexOf('/whep/') === -1) throw new Error(`invalid WHEP url ${url}`);
        if (options?.videoOnly && options?.audioOnly) throw new Error(`The videoOnly and audioOnly in options can't be true at the same time`);

        if (!options?.videoOnly) self.pc.addTransceiver("audio", {direction: "recvonly"});
        if (!options?.audioOnly) self.pc.addTransceiver("video", {direction: "recvonly"});

        var offer = await self.pc.createOffer();
        await self.pc.setLocalDescription(offer);
        const answer = await new Promise(function(resolve, reject) {
            console.log(`Generated offer: ${offer.sdp}`);

            const xhr = new XMLHttpRequest();
            xhr.onload = function() {
                if (xhr.readyState !== xhr.DONE) return;
                if (xhr.status !== 200 && xhr.status !== 201) return reject(xhr);
                const data = xhr.responseText;
                console.log("Got answer: ", data);
                return data.code ? reject(xhr) : resolve(data);
            }
            xhr.open('POST', url, true);
            xhr.setRequestHeader('Content-type', 'application/sdp');
            xhr.send(offer.sdp);
        });
        await self.pc.setRemoteDescription(
            new RTCSessionDescription({type: 'answer', sdp: answer})
        );

        return self.__internal.parseId(url, offer.sdp, answer);
    };

    // Close the publisher.
    self.close = function () {
        self.pc && self.pc.close();
        self.pc = null;
    };

    // The callback when got local stream.
    // @see https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/addStream#Migrating_to_addTrack
    self.ontrack = function (event) {
        // Add track to stream of SDK.
        self.stream.addTrack(event.track);
    };

    self.pc = new RTCPeerConnection(null);

    // To keep api consistent between player and publisher.
    // @see https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/addStream#Migrating_to_addTrack
    // @see https://webrtc.org/getting-started/media-devices
    self.stream = new MediaStream();

    // Internal APIs.
    self.__internal = {
        parseId: (url, offer, answer) => {
            let sessionid = offer.substr(offer.indexOf('a=ice-ufrag:') + 'a=ice-ufrag:'.length);
            sessionid = sessionid.substr(0, sessionid.indexOf('\n') - 1) + ':';
            sessionid += answer.substr(answer.indexOf('a=ice-ufrag:') + 'a=ice-ufrag:'.length);
            sessionid = sessionid.substr(0, sessionid.indexOf('\n'));

            const a = document.createElement("a");
            a.href = url;
            return {
                sessionid: sessionid, // Should be ice-ufrag of answer:offer.
                simulator: a.protocol + '//' + a.host + '/rtc/v1/nack/',
            };
        },
    };

    // https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/ontrack
    self.pc.ontrack = function(event) {
        if (self.ontrack) {
            self.ontrack(event);
        }
    };

    return self;
}

// Format the codec of RTCRtpSender, kind(audio/video) is optional filter.
// https://developer.mozilla.org/en-US/docs/Web/Media/Formats/WebRTC_codecs#getting_the_supported_codecs
function SrsRtcFormatSenders(senders, kind) {
    var codecs = [];
    senders.forEach(function (sender) {
        var params = sender.getParameters();
        params && params.codecs && params.codecs.forEach(function(c) {
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

