// The SRS test page: one job per URL, results in window.srsTest.
//
// The pure helpers are exported for the Node unit tests; the browser part
// runs only when the page loads it.
(function (root, factory) {
  'use strict';
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.SrsTest = api;
}(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const MODES = ['whip', 'whep', 'flv', 'ts', 'hls'];
  const CODECS = { h264: 'video/H264', h265: 'video/H265', vp9: 'video/VP9', av1: 'video/AV1' };
  const SAMPLE_INTERVAL_MS = 1000;

  function parseOptions(search) {
    const q = new URLSearchParams(search);
    const mode = q.get('mode');
    if (!MODES.includes(mode)) {
      throw new Error(`mode must be one of ${MODES.join(', ')}; got "${mode === null ? '' : mode}"`);
    }
    const url = q.get('url');
    if (!url) throw new Error('url is missing: the stream or WHIP/WHEP URL');
    let parsed;
    try {
      parsed = new URL(url);
    } catch (e) {
      throw new Error(`url is not a URL: ${url}`);
    }
    if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') {
      throw new Error(`url must be http or https: ${url}`);
    }
    let codec = q.get('codec');
    if (codec) {
      codec = codec.toLowerCase();
      if (codec === 'vp8') throw new Error('codec vp8 is not supported by SRS');
      if (!CODECS[codec]) throw new Error(`codec must be one of ${Object.keys(CODECS).join(', ')}; got "${codec}"`);
      if (mode !== 'whip' && mode !== 'whep') throw new Error(`codec applies only to whip and whep, not ${mode}`);
    }
    return { mode, url, codec: codec || null };
  }

  // SRS negotiates H.264 for a WHIP offer unless the URL names the codec.
  function signalingUrl(opts) {
    if (opts.mode !== 'whip' || !opts.codec) return opts.url;
    const u = new URL(opts.url);
    u.searchParams.set('vcodec', opts.codec);
    return u.href;
  }

  function codecMime(codec) {
    return CODECS[codec];
  }

  function preferredCodecs(codecs, codec) {
    const mime = codecMime(codec).toLowerCase();
    const chosen = codecs.filter((c) => c.mimeType.toLowerCase() === mime);
    if (!chosen.length) return [];
    return chosen.concat(codecs.filter((c) => c.mimeType.toLowerCase() === 'video/rtx'));
  }

  function resolveLocation(location, base) {
    return location ? new URL(location, base).href : null;
  }

  function pick(s, fields, zeroWhenAbsent) {
    const o = {};
    for (const f of fields) {
      if (s[f] !== undefined) o[f] = s[f];
      else o[f] = zeroWhenAbsent && zeroWhenAbsent.includes(f) ? 0 : null;
    }
    return o;
  }

  // Chrome leaves the retransmission and NACK keys out until the first one.
  const INBOUND_VIDEO = ['packetsReceived', 'packetsLost', 'nackCount', 'retransmittedPacketsReceived',
    'retransmittedBytesReceived', 'framesDecoded', 'framesDropped', 'framesPerSecond', 'frameWidth',
    'frameHeight', 'freezeCount', 'totalFreezesDuration', 'jitter', 'pliCount'];
  const INBOUND_AUDIO = ['packetsReceived', 'packetsLost', 'totalAudioEnergy', 'concealedSamples',
    'totalSamplesReceived', 'jitter'];
  const OUTBOUND_VIDEO = ['packetsSent', 'framesEncoded', 'nackCount', 'retransmittedPacketsSent',
    'retransmittedBytesSent', 'pliCount', 'qualityLimitationReason', 'frameWidth', 'frameHeight'];
  const OUTBOUND_AUDIO = ['packetsSent', 'nackCount', 'retransmittedPacketsSent', 'retransmittedBytesSent'];
  const ZERO = ['nackCount', 'retransmittedPacketsReceived', 'retransmittedBytesReceived',
    'retransmittedPacketsSent', 'retransmittedBytesSent'];

  function summarizeStats(entries) {
    const byId = new Map(entries.map((s) => [s.id, s]));
    const rtp = (type, kind, fields) => {
      const s = entries.find((e) => e.type === type && e.kind === kind);
      if (!s) return null;
      const codec = s.codecId && byId.get(s.codecId) ? byId.get(s.codecId).mimeType : null;
      return Object.assign({ codec }, pick(s, fields, ZERO));
    };
    const transport = entries.find((e) => e.type === 'transport' && e.selectedCandidatePairId);
    let pair = transport ? byId.get(transport.selectedCandidatePairId) : null;
    if (!pair) pair = entries.find((e) => e.type === 'candidate-pair' && e.nominated && e.state === 'succeeded');
    return {
      inboundVideo: rtp('inbound-rtp', 'video', INBOUND_VIDEO),
      inboundAudio: rtp('inbound-rtp', 'audio', INBOUND_AUDIO),
      outboundVideo: rtp('outbound-rtp', 'video', OUTBOUND_VIDEO),
      outboundAudio: rtp('outbound-rtp', 'audio', OUTBOUND_AUDIO),
      pair: pair ? pick(pair, ['state', 'nominated', 'currentRoundTripTime']) : null,
    };
  }

  function summarizeMedia(el) {
    const q = typeof el.getVideoPlaybackQuality === 'function' ? el.getVideoPlaybackQuality() : null;
    return {
      currentTime: el.currentTime,
      videoWidth: el.videoWidth,
      videoHeight: el.videoHeight,
      paused: el.paused,
      readyState: el.readyState,
      droppedVideoFrames: q ? q.droppedVideoFrames : null,
      totalVideoFrames: q ? q.totalVideoFrames : null,
      audioDecodedBytes: el.webkitAudioDecodedByteCount === undefined ? null : el.webkitAudioDecodedByteCount,
    };
  }

  // The browser part. States: starting, signaling, connected, playing,
  // error, unsupported, stopped.
  function start(win) {
    const doc = win.document;
    const video = doc.getElementById('video');
    const started = Date.now();
    const t = {
      mode: null, url: null, codec: null, state: 'starting', error: null,
      location: null, events: [], samples: [], stop: null,
    };
    win.srsTest = t;

    let pc = null;
    let stream = null;
    let player = null;
    let timer = null;

    const event = (name, detail) => {
      t.events.push(detail === undefined ? { t: Date.now() - started, name } : { t: Date.now() - started, name, detail });
    };
    const setState = (state) => {
      if (t.state === 'error' || t.state === 'unsupported' || t.state === 'stopped') return;
      t.state = state;
      event(state);
    };
    const fail = (message, state) => {
      if (t.state === 'error' || t.state === 'unsupported' || t.state === 'stopped') return;
      t.error = message;
      t.state = state || 'error';
      event(t.state, message);
    };

    const sample = async () => {
      const now = Date.now();
      const s = { t: now - started, at: now };
      try {
        if (pc) {
          const entries = [];
          (await pc.getStats()).forEach((e) => entries.push(e));
          Object.assign(s, summarizeStats(entries));
        }
        if (t.mode !== 'whip') s.media = summarizeMedia(video);
      } catch (e) {
        s.error = e.message;
      }
      t.samples.push(s);
    };

    const signal = async (label, url, sdp) => {
      let res;
      try {
        res = await win.fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/sdp' }, body: sdp });
      } catch (e) {
        throw new Error(`${label} ${url} did not answer: ${e.message}`);
      }
      const body = await res.text();
      if (!res.ok) throw new Error(`${label} ${url} answered HTTP ${res.status}: ${body.slice(0, 300)}`);
      t.location = resolveLocation(res.headers.get('Location'), url);
      return body;
    };

    const watchPeer = () => {
      pc.onconnectionstatechange = () => {
        event('pc', pc.connectionState);
        if (pc.connectionState === 'connected') setState('connected');
        if (pc.connectionState === 'failed') fail(`${t.mode.toUpperCase()} connection failed`);
      };
    };

    const webrtc = async (opts) => {
      const label = opts.mode.toUpperCase();
      pc = new win.RTCPeerConnection();
      watchPeer();
      const send = opts.mode === 'whip';
      if (send) {
        stream = await win.navigator.mediaDevices.getUserMedia({ audio: true, video: true });
        for (const track of stream.getTracks()) pc.addTransceiver(track, { direction: 'sendonly', streams: [stream] });
      } else {
        const remote = new win.MediaStream();
        pc.addTransceiver('audio', { direction: 'recvonly' });
        pc.addTransceiver('video', { direction: 'recvonly' });
        // Without a sink Chrome does not decode the audio. Attach on the
        // first track: an empty stream holds back the page's load event.
        pc.ontrack = (e) => {
          remote.addTrack(e.track);
          if (!video.srcObject) video.srcObject = remote;
          event('track', e.track.kind);
        };
      }
      if (opts.codec) {
        const caps = (send ? win.RTCRtpSender : win.RTCRtpReceiver).getCapabilities('video');
        const prefs = preferredCodecs(caps ? caps.codecs : [], opts.codec);
        if (!prefs.length) {
          fail(`codec ${opts.codec} is not in the browser's ${send ? 'send' : 'receive'} capabilities`, 'unsupported');
          return;
        }
        const tr = pc.getTransceivers().find((x) => (x.sender.track ? x.sender.track.kind : x.receiver.track.kind) === 'video');
        tr.setCodecPreferences(prefs);
      }
      const offer = await pc.createOffer();
      await pc.setLocalDescription(offer);
      setState('signaling');
      const answer = await signal(label, signalingUrl(opts), offer.sdp);
      await pc.setRemoteDescription({ type: 'answer', sdp: answer });
    };

    const http = (opts) => {
      const label = opts.mode.toUpperCase();
      setState('signaling');
      if (opts.mode === 'hls') {
        const Hls = win.Hls;
        if (!Hls || !Hls.isSupported()) {
          fail('HLS hls.js is not supported in this browser', 'unsupported');
          return;
        }
        player = new Hls({ liveDurationInfinity: true });
        player.on(Hls.Events.ERROR, (_e, d) => {
          if (d.fatal) fail(`${label} ${opts.url}: ${d.type} ${d.details}${d.response ? ` HTTP ${d.response.code}` : ''}`);
        });
        player.loadSource(opts.url);
        player.attachMedia(video);
      } else {
        const mpegts = win.mpegts;
        if (!mpegts || !mpegts.isSupported()) {
          fail(`${label} mpegts.js is not supported in this browser`, 'unsupported');
          return;
        }
        player = mpegts.createPlayer({ type: opts.mode === 'flv' ? 'flv' : 'mpegts', isLive: true, url: opts.url });
        player.on(mpegts.Events.ERROR, (type, detail, info) => {
          fail(`${label} ${opts.url}: ${type} ${detail}${info && info.msg ? ` ${info.msg}` : ''}${info && info.code ? ` HTTP ${info.code}` : ''}`);
        });
        player.attachMediaElement(video);
        player.load();
      }
      const played = video.play();
      if (played && played.catch) played.catch((e) => event('play-rejected', e.message));
    };

    t.stop = async () => {
      if (timer) win.clearInterval(timer);
      timer = null;
      if (t.state !== 'error' && t.state !== 'unsupported') await sample();
      if (t.location) {
        try {
          const res = await win.fetch(t.location, { method: 'DELETE' });
          event('delete', res.status);
        } catch (e) {
          event('delete', e.message);
        }
      }
      if (player) {
        try { player.destroy(); } catch (e) { event('destroy', e.message); }
      }
      if (pc) pc.close();
      if (stream) stream.getTracks().forEach((tr) => tr.stop());
      if (t.state !== 'error' && t.state !== 'unsupported') t.state = 'stopped';
      event('stopped');
      return t.state;
    };

    let opts;
    try {
      opts = parseOptions(win.location.search);
    } catch (e) {
      fail(e.message);
      return t;
    }
    t.mode = opts.mode;
    t.url = opts.url;
    t.codec = opts.codec;
    video.addEventListener('playing', () => setState('playing'));
    timer = win.setInterval(sample, SAMPLE_INTERVAL_MS);

    const run = opts.mode === 'whip' || opts.mode === 'whep' ? webrtc(opts) : Promise.resolve(http(opts));
    run.catch((e) => fail(e.message.startsWith(opts.mode.toUpperCase()) ? e.message : `${opts.mode.toUpperCase()} ${e.message}`));
    return t;
  }

  return {
    MODES,
    parseOptions,
    signalingUrl,
    codecMime,
    preferredCodecs,
    resolveLocation,
    summarizeStats,
    summarizeMedia,
    start,
  };
}));
