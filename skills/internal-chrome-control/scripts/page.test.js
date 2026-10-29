'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');

const page = require('../page/srs-test.js');
const chrome = require('./chrome.js');

test('parseOptions reads each mode with its stream URL', () => {
  for (const mode of ['whip', 'whep', 'flv', 'ts', 'hls']) {
    const url = 'http://127.0.0.1:1985/x?app=live&stream=s';
    const o = page.parseOptions(`?mode=${mode}&url=${encodeURIComponent(url)}`);
    assert.deepStrictEqual(o, { mode, url, codec: null });
  }
});

test('parseOptions reads a codec for WHIP and WHEP, in any case', () => {
  for (const codec of ['h264', 'h265', 'vp9', 'av1']) {
    const o = page.parseOptions(`?mode=whip&url=http%3A%2F%2Fh%2Fw&codec=${codec.toUpperCase()}`);
    assert.strictEqual(o.codec, codec);
  }
  assert.strictEqual(page.parseOptions('?mode=whep&url=http%3A%2F%2Fh%2Fw&codec=vp9').codec, 'vp9');
});

test('parseOptions names what is wrong', () => {
  const bad = [
    ['', /mode/],
    ['?url=http%3A%2F%2Fh%2Fw', /mode/],
    ['?mode=rtmp&url=http%3A%2F%2Fh%2Fw', /mode must be one of whip, whep, flv, ts, hls.*rtmp/],
    ['?mode=whep', /url/],
    ['?mode=whep&url=not-a-url', /url.*not-a-url/],
    ['?mode=whep&url=rtmp%3A%2F%2Fh%2Flive%2Fs', /url.*http/],
    ['?mode=whip&url=http%3A%2F%2Fh%2Fw&codec=vp8', /vp8.*SRS/i],
    ['?mode=whip&url=http%3A%2F%2Fh%2Fw&codec=mpeg2', /codec must be one of h264, h265, vp9, av1.*mpeg2/],
    ['?mode=flv&url=http%3A%2F%2Fh%2Fs.flv&codec=h264', /codec.*whip.*whep/i],
  ];
  for (const [search, re] of bad) {
    assert.throws(() => page.parseOptions(search), re, search);
  }
});

test('the WHIP URL names the codec so SRS negotiates it; WHEP plays the source codec', () => {
  const url = 'http://127.0.0.1:1985/rtc/v1/whip/?app=live&stream=s';
  const whip = new URL(page.signalingUrl({ mode: 'whip', url, codec: 'vp9' }));
  assert.strictEqual(whip.searchParams.get('vcodec'), 'vp9');
  assert.strictEqual(whip.searchParams.get('stream'), 's');
  assert.strictEqual(page.signalingUrl({ mode: 'whip', url, codec: null }), url);
  const whep = 'http://127.0.0.1:1985/rtc/v1/whep/?app=live&stream=s';
  assert.strictEqual(page.signalingUrl({ mode: 'whep', url: whep, codec: 'vp9' }), whep);
});

test('codecMime maps the codec names to RTP MIME types', () => {
  assert.strictEqual(page.codecMime('h264'), 'video/H264');
  assert.strictEqual(page.codecMime('h265'), 'video/H265');
  assert.strictEqual(page.codecMime('vp9'), 'video/VP9');
  assert.strictEqual(page.codecMime('av1'), 'video/AV1');
});

test('preferredCodecs keeps the chosen codec and RTX, nothing else', () => {
  const caps = [
    { mimeType: 'video/VP8' },
    { mimeType: 'video/rtx' },
    { mimeType: 'video/H264', sdpFmtpLine: 'profile-level-id=42e01f' },
    { mimeType: 'video/VP9', sdpFmtpLine: 'profile-id=0' },
    { mimeType: 'video/h264', sdpFmtpLine: 'profile-level-id=640034' },
    { mimeType: 'video/red' },
  ];
  assert.deepStrictEqual(page.preferredCodecs(caps, 'h264'), [caps[2], caps[4], caps[1]]);
  assert.deepStrictEqual(page.preferredCodecs(caps, 'vp9'), [caps[3], caps[1]]);
  assert.deepStrictEqual(page.preferredCodecs(caps, 'av1'), []);
});

test('resolveLocation turns the WHIP answer Location into an absolute URL', () => {
  const base = 'http://127.0.0.1:1985/rtc/v1/whip/?app=live&stream=s';
  assert.strictEqual(page.resolveLocation('/rtc/v1/whip/?action=delete&token=t', base),
    'http://127.0.0.1:1985/rtc/v1/whip/?action=delete&token=t');
  assert.strictEqual(page.resolveLocation('http://other:1/x', base), 'http://other:1/x');
  assert.strictEqual(page.resolveLocation(null, base), null);
  assert.strictEqual(page.resolveLocation('', base), null);
});

function playerStats() {
  return [
    { id: 'T', type: 'transport', selectedCandidatePairId: 'CP1' },
    { id: 'CP0', type: 'candidate-pair', state: 'failed', nominated: false, currentRoundTripTime: 9 },
    { id: 'CP1', type: 'candidate-pair', state: 'succeeded', nominated: true, currentRoundTripTime: 0.001 },
    { id: 'CV', type: 'codec', mimeType: 'video/H264' },
    { id: 'CA', type: 'codec', mimeType: 'audio/opus' },
    {
      id: 'IV', type: 'inbound-rtp', kind: 'video', codecId: 'CV', packetsReceived: 310, packetsLost: -1,
      nackCount: 1, framesDecoded: 121, framesDropped: 0, frameWidth: 640, frameHeight: 480,
      freezeCount: 1, totalFreezesDuration: 0.33, jitter: 0.001, pliCount: 2,
    },
    {
      id: 'IA', type: 'inbound-rtp', kind: 'audio', codecId: 'CA', packetsReceived: 302, packetsLost: 15,
      totalAudioEnergy: 5.3, concealedSamples: 14100, totalSamplesReceived: 289000, jitter: 0.002,
    },
  ];
}

test('summarizeStats picks the play-side fields and reads absent RTX counters as 0', () => {
  const s = page.summarizeStats(playerStats());
  assert.deepStrictEqual(s.inboundVideo, {
    codec: 'video/H264', packetsReceived: 310, packetsLost: -1, nackCount: 1, retransmittedPacketsReceived: 0,
    retransmittedBytesReceived: 0, framesDecoded: 121, framesDropped: 0, framesPerSecond: null,
    frameWidth: 640, frameHeight: 480, freezeCount: 1, totalFreezesDuration: 0.33, jitter: 0.001, pliCount: 2,
  });
  assert.deepStrictEqual(s.inboundAudio, {
    codec: 'audio/opus', packetsReceived: 302, packetsLost: 15, totalAudioEnergy: 5.3,
    concealedSamples: 14100, totalSamplesReceived: 289000, jitter: 0.002,
  });
  assert.strictEqual(s.outboundVideo, null);
  assert.strictEqual(s.outboundAudio, null);
  assert.deepStrictEqual(s.pair, { state: 'succeeded', nominated: true, currentRoundTripTime: 0.001 });
});

test('summarizeStats picks the publish-side fields', () => {
  const s = page.summarizeStats([
    { id: 'CV', type: 'codec', mimeType: 'video/VP9' },
    { id: 'CA', type: 'codec', mimeType: 'audio/opus' },
    {
      id: 'OV', type: 'outbound-rtp', kind: 'video', codecId: 'CV', packetsSent: 900, framesEncoded: 200,
      nackCount: 1, retransmittedPacketsSent: 15, retransmittedBytesSent: 13600, pliCount: 1,
      qualityLimitationReason: 'none', frameWidth: 640, frameHeight: 480,
    },
    { id: 'OA', type: 'outbound-rtp', kind: 'audio', codecId: 'CA', packetsSent: 500 },
    { id: 'CP', type: 'candidate-pair', state: 'succeeded', nominated: true, currentRoundTripTime: 0 },
  ]);
  assert.deepStrictEqual(s.outboundVideo, {
    codec: 'video/VP9', packetsSent: 900, framesEncoded: 200, nackCount: 1, retransmittedPacketsSent: 15,
    retransmittedBytesSent: 13600, pliCount: 1, qualityLimitationReason: 'none', frameWidth: 640, frameHeight: 480,
  });
  assert.deepStrictEqual(s.outboundAudio, {
    codec: 'audio/opus', packetsSent: 500, nackCount: 0, retransmittedPacketsSent: 0, retransmittedBytesSent: 0,
  });
  assert.strictEqual(s.inboundVideo, null);
  assert.deepStrictEqual(s.pair, { state: 'succeeded', nominated: true, currentRoundTripTime: 0 });
});

test('summarizeStats has no pair before ICE succeeds', () => {
  const s = page.summarizeStats([{ id: 'CP', type: 'candidate-pair', state: 'in-progress', nominated: false }]);
  assert.strictEqual(s.pair, null);
});

test('summarizeMedia reads the HTTP player element', () => {
  const s = page.summarizeMedia({
    currentTime: 3.5, videoWidth: 640, videoHeight: 480, paused: false, readyState: 4,
    webkitAudioDecodedByteCount: 1234,
    getVideoPlaybackQuality: () => ({ droppedVideoFrames: 1, totalVideoFrames: 70 }),
  });
  assert.deepStrictEqual(s, {
    currentTime: 3.5, videoWidth: 640, videoHeight: 480, paused: false, readyState: 4,
    droppedVideoFrames: 1, totalVideoFrames: 70, audioDecodedBytes: 1234,
  });
  const bare = page.summarizeMedia({ currentTime: 0, videoWidth: 0, videoHeight: 0, paused: true, readyState: 0 });
  assert.strictEqual(bare.droppedVideoFrames, null);
  assert.strictEqual(bare.audioDecodedBytes, null);
});

// A stub signaling and HTTP server: it answers every request with an error
// and records what the page sent, the way SRS does for a missing stream.
function startStub() {
  const seen = [];
  const server = http.createServer((req, res) => {
    let body = '';
    req.on('data', (d) => { body += d; });
    req.on('end', () => {
      res.setHeader('Access-Control-Allow-Origin', '*');
      res.setHeader('Access-Control-Allow-Headers', '*');
      res.setHeader('Access-Control-Allow-Methods', '*');
      if (req.method === 'OPTIONS') {
        res.writeHead(200);
        res.end();
        return;
      }
      seen.push({ method: req.method, url: req.url, type: req.headers['content-type'], body });
      if (req.method === 'POST') {
        res.writeHead(500, { 'Content-Type': 'text/plain' });
        res.end('stub: stream not found');
        return;
      }
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end('stub: not found');
    });
  });
  return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({ server, seen, port: server.address().port })));
}

function listen(server) {
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => resolve(server.address().port));
  });
}

async function waitState(tab, states, ms) {
  const deadline = Date.now() + ms;
  for (;;) {
    const st = await tab.evaluate(() => (window.srsTest ? { state: window.srsTest.state, error: window.srsTest.error } : null));
    if (st && states.includes(st.state)) return st;
    if (Date.now() > deadline) return st;
    await new Promise((r) => setTimeout(r, 100));
  }
}

test('the page in Chrome reports a clear error when the server refuses or is missing', async (t) => {
  const executablePath = chrome.findBrowser({ platform: process.platform, env: process.env, exists: chrome.isExecutable });
  let puppeteer = null;
  try { puppeteer = require('puppeteer-core'); } catch { /* skipped below */ }
  if (!executablePath || !puppeteer) {
    t.skip('no browser or puppeteer-core');
    return;
  }

  const stub = await startStub();
  const pageServer = chrome.createPageServer(chrome.defaultServeDirs());
  const pagePort = await listen(pageServer);
  const userDataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'chrome-control-test-'));
  const browser = await puppeteer.launch(chrome.launchOptions({ executablePath, userDataDir, headed: false }));
  const open = async (query) => {
    const tab = await browser.newPage();
    await tab.goto(`http://127.0.0.1:${pagePort}/?${query}`);
    return tab;
  };
  const stubUrl = (p) => encodeURIComponent(`http://127.0.0.1:${stub.port}${p}`);

  try {
    await t.test('bad options', async () => {
      const tab = await open('mode=rtmp&url=x');
      const st = await waitState(tab, ['error'], 5000);
      assert.strictEqual(st.state, 'error', JSON.stringify(st));
      assert.match(st.error, /mode must be one of/);
    });

    await t.test('WHEP answered with HTTP 500', async () => {
      const tab = await open(`mode=whep&url=${stubUrl('/rtc/v1/whep/?app=live&stream=s')}`);
      const st = await waitState(tab, ['error'], 10000);
      assert.strictEqual(st.state, 'error', JSON.stringify(st));
      assert.match(st.error, /WHEP .*HTTP 500.*stub: stream not found/);
      const post = stub.seen.find((r) => r.method === 'POST' && r.url.startsWith('/rtc/v1/whep/'));
      assert.ok(post, JSON.stringify(stub.seen));
      assert.strictEqual(post.type, 'application/sdp');
      assert.match(post.body, /^v=0/);
      assert.match(post.body, /a=recvonly/);
      const samples = await tab.evaluate(() => Array.isArray(window.srsTest.samples));
      assert.strictEqual(samples, true);
      // The sampler keeps going; each sample has the wall clock beside the page time.
      const before = Date.now();
      let first = null;
      for (let i = 0; i < 30 && !first; i++) {
        first = await tab.evaluate(() => window.srsTest.samples[0] || null);
        if (!first) await new Promise((r) => { setTimeout(r, 100); });
      }
      assert.ok(first, 'no sample in 3 s');
      assert.strictEqual(typeof first.t, 'number');
      assert.ok(Math.abs(first.at - before) < 5000, `at ${first.at}, now ${before}`);
    });

    await t.test('WHIP prefers the codec and names it in the URL', async () => {
      const tab = await open(`mode=whip&codec=vp9&url=${stubUrl('/rtc/v1/whip/?app=live&stream=c')}`);
      const st = await waitState(tab, ['error'], 10000);
      assert.match(st.error, /WHIP .*HTTP 500/);
      const post = stub.seen.find((r) => r.method === 'POST' && r.url.startsWith('/rtc/v1/whip/'));
      assert.ok(post, JSON.stringify(stub.seen));
      assert.strictEqual(new URL(post.url, 'http://x').searchParams.get('vcodec'), 'vp9');
      const mline = post.body.split('\r\n').find((l) => l.startsWith('m=video'));
      const firstPt = mline.split(' ')[3];
      assert.match(post.body, new RegExp(`a=rtpmap:${firstPt} VP9/90000`));
      assert.doesNotMatch(post.body, /a=rtpmap:\d+ VP8\//);
      assert.match(post.body, /a=sendonly/);
    });

    await t.test('WHEP server not listening', async () => {
      const closed = http.createServer();
      const port = await listen(closed);
      await new Promise((r) => closed.close(r));
      const tab = await open(`mode=whep&url=${encodeURIComponent(`http://127.0.0.1:${port}/rtc/v1/whep/?app=live&stream=s`)}`);
      const st = await waitState(tab, ['error'], 10000);
      assert.match(st.error, /WHEP .*did not answer/);
    });

    for (const [mode, file] of [['flv', '/live/s.flv'], ['ts', '/live/s.ts'], ['hls', '/live/s.m3u8']]) {
      await t.test(`${mode} stream missing`, async () => {
        const tab = await open(`mode=${mode}&url=${stubUrl(file)}`);
        const st = await waitState(tab, ['error'], 15000);
        assert.strictEqual(st.state, 'error', JSON.stringify(st));
        assert.match(st.error, new RegExp(`^${mode.toUpperCase()} `));
        assert.ok(stub.seen.some((r) => r.method === 'GET' && r.url === file), JSON.stringify(stub.seen));
      });
    }
  } finally {
    await browser.close();
    fs.rmSync(userDataDir, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
    pageServer.close();
    stub.server.close();
  }
});
