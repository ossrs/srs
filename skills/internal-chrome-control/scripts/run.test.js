'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const http = require('node:http');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const chrome = require('./chrome.js');

const SCRIPT = path.join(__dirname, 'chrome.js');
const WHIP = 'http://127.0.0.1:1985/rtc/v1/whip/?app=live&stream=s';
const WHEP = 'http://127.0.0.1:1985/rtc/v1/whep/?app=live&stream=s';

test('parseArgs reads run jobs, each option after its --mode', () => {
  const o = chrome.parseArgs(['run', '--mode', 'whip', '--url', WHIP, '--codec', 'VP9', '--mode', 'whep', '--url', WHEP]);
  assert.strictEqual(o.command, 'run');
  assert.deepStrictEqual(o.jobs, [
    { name: 'whip', mode: 'whip', url: WHIP, codec: 'vp9' },
    { name: 'whep', mode: 'whep', url: WHEP, codec: null },
  ]);
  assert.strictEqual(o.seconds, 5);
  assert.strictEqual(o.timeout, null);
  assert.strictEqual(o.out, null);
  assert.strictEqual(o.headed, false);
  assert.strictEqual(o.logDir, null);
  assert.strictEqual(o.pagePort, 0);
  assert.strictEqual(o.internals, null);
  assert.strictEqual(o.allowLoopback, false);
});

test('parseArgs reads --allow-loopback for the run', () => {
  const o = chrome.parseArgs(['run', '--mode', 'whep', '--url', WHEP, '--allow-loopback']);
  assert.strictEqual(o.allowLoopback, true);
  assert.throws(() => chrome.parseArgs(['launch', '--probe', '--allow-loopback']), /unknown option --allow-loopback/);
});

test('parseArgs reads the run options anywhere on the line', () => {
  const o = chrome.parseArgs(['run', '--seconds', '3', '--mode', 'flv', '--url', 'http://h:8080/live/s.flv',
    '--out', '/tmp/s.json', '--timeout', '15', '--headed', '--log-dir', '/tmp/l', '--page-port', '15609', '--name', 'f1',
    '--internals', '/tmp/i.json']);
  assert.deepStrictEqual(o.jobs, [{ name: 'f1', mode: 'flv', url: 'http://h:8080/live/s.flv', codec: null }]);
  assert.strictEqual(o.seconds, 3);
  assert.strictEqual(o.timeout, 15);
  assert.strictEqual(o.out, '/tmp/s.json');
  assert.strictEqual(o.headed, true);
  assert.strictEqual(o.logDir, '/tmp/l');
  assert.strictEqual(o.pagePort, 15609);
  assert.strictEqual(o.internals, '/tmp/i.json');
});

test('parseArgs names repeated jobs apart', () => {
  const o = chrome.parseArgs(['run', '--mode', 'whip', '--url', WHIP, '--codec', 'h264', '--mode', 'whip', '--url', WHIP, '--codec', 'av1',
    '--mode', 'whep', '--url', WHEP, '--mode', 'whep', '--url', WHEP]);
  assert.deepStrictEqual(o.jobs.map((j) => j.name), ['whip-h264', 'whip-av1', 'whep-1', 'whep-2']);
});

test('parseArgs rejects a bad run line with the reason', () => {
  const bad = [
    [[], /--mode/],
    [['--url', WHEP], /--url.*--mode/],
    [['--mode', 'rtmp', '--url', WHEP], /mode must be one of/],
    [['--mode', 'whep'], /url is missing/],
    [['--mode', 'whep', '--url', 'rtmp://h/live/s'], /http/],
    [['--mode', 'whip', '--url', WHIP, '--codec', 'vp8'], /vp8/],
    [['--mode', 'flv', '--url', 'http://h/live/s.flv', '--codec', 'h264'], /codec/],
    [['--mode', 'whep', '--url', WHEP, '--name', 'a', '--mode', 'whep', '--url', WHEP, '--name', 'a'], /name a.*twice/],
    [['--mode', 'whep', '--url', WHEP, '--name', 'a b'], /--name/],
    [['--mode', 'whep', '--url', WHEP, '--url', WHEP], /--url.*twice/],
    [['--mode', 'whep', '--url', WHEP, '--seconds', '0'], /--seconds/],
    [['--mode', 'whep', '--url', WHEP, '--seconds', 'x'], /--seconds/],
    [['--mode', 'whep', '--url', WHEP, '--timeout', '0'], /--timeout/],
    [['--mode', 'whep', '--url', WHEP, '--page-port', '70000'], /--page-port/],
    [['--mode', 'whep', '--url', WHEP, '--out'], /--out/],
    [['--mode', 'whep', '--url', WHEP, '--internals'], /--internals/],
    [['--mode', 'whep', '--url', WHEP, '--bogus'], /--bogus/],
  ];
  for (const [args, re] of bad) {
    assert.throws(() => chrome.parseArgs(['run', ...args]), (e) => e instanceof chrome.UsageError && re.test(e.message), args.join(' '));
  }
});

test('streamKey ties a WHIP job to the players of the same stream', () => {
  assert.strictEqual(chrome.streamKey(WHIP), 'live/s');
  assert.strictEqual(chrome.streamKey(WHEP), 'live/s');
  assert.strictEqual(chrome.streamKey('http://h:8080/live/s.flv'), 'live/s');
  assert.strictEqual(chrome.streamKey('http://h:8080/live/s.ts'), 'live/s');
  assert.strictEqual(chrome.streamKey('http://h:8080/live/s.m3u8?x=1'), 'live/s');
  assert.strictEqual(chrome.streamKey('http://h:1985/rtc/v1/whep/?app=live&stream=other'), 'live/other');
  assert.strictEqual(chrome.streamKey('http://h:1985/rtc/v1/whep/?stream=s'), 'live/s');
});

test('readyStates: a publisher is ready when connected, a player when playing', () => {
  assert.deepStrictEqual(chrome.readyStates('whip'), ['connected']);
  for (const m of ['whep', 'flv', 'ts', 'hls']) assert.deepStrictEqual(chrome.readyStates(m), ['playing']);
});

test('the ready timeout is 10 s, 20 s for H.265, or what --timeout says', () => {
  assert.strictEqual(chrome.readyTimeoutMs({ codec: null }, null, 50), 500);
  assert.strictEqual(chrome.readyTimeoutMs({ codec: null }, null), 10000);
  assert.strictEqual(chrome.readyTimeoutMs({ codec: 'av1' }, null), 10000);
  assert.strictEqual(chrome.readyTimeoutMs({ codec: 'h265' }, null), 20000);
  assert.strictEqual(chrome.readyTimeoutMs({ codec: 'h265' }, 5), 5000);
  assert.strictEqual(chrome.readyTimeoutMs({ codec: null, streamCodec: 'h265' }, null), 20000);
  assert.strictEqual(chrome.readyTimeoutMs({ codec: null, streamCodec: 'h264' }, null), 10000);
});

test('progress counts from the last sample before ready to the last sample', () => {
  const whep = {
    mode: 'whep',
    events: [{ t: 10, name: 'signaling' }, { t: 1500, name: 'playing' }],
    samples: [
      { t: 1000, inboundVideo: null },
      { t: 2000, inboundVideo: { framesDecoded: 10 } },
      { t: 3000, inboundVideo: { framesDecoded: 30 } },
      { t: 3400, inboundVideo: { framesDecoded: 38 } },
    ],
  };
  assert.deepStrictEqual(chrome.progress(whep), { metric: 'framesDecoded', from: 0, to: 38, ms: 1900 });
  const flv = {
    mode: 'flv',
    events: [{ t: 900, name: 'playing' }],
    samples: [{ t: 500, media: { currentTime: 0 } }, { t: 1500, media: { currentTime: 0.5 } }, { t: 5500, media: { currentTime: 4.5 } }],
  };
  assert.deepStrictEqual(chrome.progress(flv), { metric: 'currentTime', from: 0, to: 4.5, ms: 4600 });
  const whip = {
    mode: 'whip',
    events: [{ t: 300, name: 'connected' }],
    samples: [{ t: 1000, outboundVideo: { framesEncoded: 14 } }, { t: 2000, outboundVideo: { framesEncoded: 34 } }],
  };
  assert.deepStrictEqual(chrome.progress(whip), { metric: 'framesEncoded', from: 0, to: 34, ms: 1700 });
  assert.strictEqual(chrome.progress({ mode: 'whep', events: [{ t: 1, name: 'playing' }], samples: [] }), null);
});

test('the exit code: SRS not answering wins, then a failure, else pass', () => {
  assert.strictEqual(chrome.exitCode([{ result: 'pass' }, { result: 'skip' }]), 0);
  assert.strictEqual(chrome.exitCode([{ result: 'skip' }]), 0);
  assert.strictEqual(chrome.exitCode([{ result: 'pass' }, { result: 'fail' }]), 1);
  assert.strictEqual(chrome.exitCode([{ result: 'fail' }, { result: 'fail', unreachable: true }]), 6);
});

// A fake browser whose windows run a scripted window.srsTest. Each job's
// script is picked by the page URL's mode and url, and timed from goto.
function fakeRun(scripts, opts = {}) {
  const log = [];
  const state = { closed: false, killed: null, launched: false, options: null, profileExisted: false };
  const proc = { pid: 4343, kill: (sig) => { state.killed = sig; } };
  const makeTab = (createOptions) => {
    let win = null;
    const tab = {
      createOptions,
      goto: async (url) => {
        if (url === 'chrome://webrtc-internals') {
          log.push({ at: Date.now(), what: 'goto', mode: 'internals', url });
          win = fakeInternals(state, log, opts.internals || {});
          tab.createCDPSession = async () => win.cdp;
          return;
        }
        const q = new URL(url).searchParams;
        const key = `${q.get('mode')} ${q.get('url')}`;
        const script = scripts[key] || scripts[q.get('mode')];
        assert.ok(script, `no fake script for ${key}`);
        if (script.gotoError) throw new Error(script.gotoError);
        log.push({ at: Date.now(), what: 'goto', mode: q.get('mode'), url: q.get('url'), codec: q.get('codec') });
        win = { srsTest: fakeSrsTest(script, q, log) };
      },
      evaluate: async (fn, ...args) => {
        globalThis.window = win;
        globalThis.document = win && win.document;
        try {
          return await fn(...args);
        } finally {
          delete globalThis.window;
          delete globalThis.document;
        }
      },
      on: () => {},
      close: async () => {},
    };
    return tab;
  };
  const browser = {
    version: async () => 'Chrome/156.0.0.0',
    process: () => proc,
    newPage: async (o) => {
      log.push({ at: Date.now(), what: 'newPage', options: o });
      return makeTab(o);
    },
    close: async () => { state.closed = true; },
  };
  const puppeteer = {
    launch: async (o) => {
      state.launched = true;
      state.options = o;
      state.profileExisted = fs.existsSync(o.userDataDir);
      if (opts.launchError) throw opts.launchError;
      return browser;
    },
  };
  return { puppeteer, state, log };
}

// chrome://webrtc-internals: its dump button downloads the dump into the
// folder the driver set over CDP. dump: the bytes; complete: false never
// finishes the download; noButton: the page has no dump button; peers: the
// peer connections it knows, one more each read up to the number.
function fakeInternals(state, log, conf) {
  const handlers = {};
  const cdp = {
    on: (event, fn) => { handlers[event] = fn; },
    send: async (method, params) => {
      log.push({ at: Date.now(), what: 'cdp', method, params });
      if (method === 'Browser.setDownloadBehavior') state.downloadPath = params.downloadPath;
      return {};
    },
  };
  const button = {
    click: () => {
      log.push({ at: Date.now(), what: 'dump' });
      if (conf.complete === false) return;
      setTimeout(() => {
        fs.writeFileSync(path.join(state.downloadPath, 'webrtc_internals_dump.gz'), conf.dump);
        handlers['Browser.downloadProgress']({ guid: 'g', state: 'completed' });
      }, 10);
    },
  };
  const win = { cdp, document: { querySelector: (sel) => (sel === '#dump-click-target' && !conf.noButton ? button : null) } };
  if (conf.peers !== undefined) {
    let known = 0;
    Object.defineProperty(win, 'peerConnectionDataStore', {
      get() {
        log.push({ at: Date.now(), what: 'peers', known });
        const store = Object.fromEntries(Array.from({ length: known }, (_, i) => [`9-${i + 1}`, {}]));
        if (known < conf.peers) known++;
        return store;
      },
    });
  }
  return win;
}

// readyMs: when the state becomes ready; rate: the metric per second.
function fakeSrsTest(script, q, log) {
  const born = Date.now();
  const mode = q.get('mode');
  const t = {
    mode, url: q.get('url'), codec: q.get('codec'), error: null, location: script.location || null,
    events: [], samples: [],
    stop: async () => {
      log.push({ at: Date.now(), what: 'stop', mode, url: t.url });
      sample();
      if (t.state !== 'error' && t.state !== 'unsupported') frozen = 'stopped';
      return t.state;
    },
  };
  let frozen = null;
  const now = () => Date.now() - born;
  const sample = () => {
    const ms = now();
    const ready = script.readyMs !== undefined && ms >= script.readyMs;
    const n = ready ? ((ms - script.readyMs) / 1000) * (script.rate || 0) : 0;
    const s = { t: ms };
    if (mode === 'whip') s.outboundVideo = { framesEncoded: Math.floor(n) };
    else if (mode === 'whep') s.inboundVideo = { framesDecoded: Math.floor(n) };
    else s.media = { currentTime: n };
    t.samples.push(s);
  };
  Object.defineProperty(t, 'state', {
    enumerable: true,
    get() {
      if (frozen) return frozen;
      const ms = now();
      if (script.error && ms >= (script.errorMs || 0)) {
        t.error = script.error;
        frozen = script.errorState || 'error';
        t.events.push({ t: ms, name: frozen, detail: script.error });
        return frozen;
      }
      if (script.readyMs !== undefined && ms >= script.readyMs) {
        const st = mode === 'whip' ? 'connected' : 'playing';
        if (!t.events.some((e) => e.name === st)) t.events.push({ t: script.readyMs, name: st });
        return st;
      }
      return 'signaling';
    },
  });
  return t;
}

function capture() {
  const lines = { out: [], err: [] };
  return { lines, out: (s) => lines.out.push(s), err: (s) => lines.err.push(s) };
}

const BASE_DEPS = {
  platform: 'linux', env: {}, exists: (p) => p === '/usr/bin/chromium',
  connect: async () => null, msPerSecond: 50, pollMs: 5,
};

async function runFake(args, scripts, extra = {}) {
  const c = capture();
  const fake = fakeRun(scripts, extra);
  const code = await chrome.main(['run', ...args], { ...BASE_DEPS, out: c.out, err: c.err, puppeteer: fake.puppeteer, ...extra.deps });
  return { code, c, fake, text: c.lines.out.concat(c.lines.err).join('\n') };
}

function tmpDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'chrome-control-run-'));
}

test('run --allow-loopback launches the browser with loopback candidates', async () => {
  const r = await runFake(['--mode', 'whep', '--url', WHEP, '--seconds', '1', '--allow-loopback'], {
    whep: { readyMs: 20, rate: 20 },
  });
  assert.strictEqual(r.code, 0, r.text);
  assert.ok(r.fake.state.options.args.includes('--allow-loopback-in-peer-connection'), r.fake.state.options.args.join(' '));
});

test('run prints the WHIP or WHEP resource of each ready job, so the caller can find its session', async () => {
  const loc = 'http://127.0.0.1:1985/rtc/v1/whip/?action=delete&token=t1&app=live&stream=s&session=ab12:cd34';
  const r = await runFake(['--mode', 'whep', '--url', WHEP, '--mode', 'flv', '--url', 'http://127.0.0.1:8080/live/s.flv', '--seconds', '1'], {
    whep: { readyMs: 20, rate: 20, location: loc },
    flv: { readyMs: 20, rate: 1 },
  });
  assert.strictEqual(r.code, 0, r.text);
  const out = r.c.lines.out;
  const ready = out.findIndex((l) => /^PASS whep ready playing in \d+ms$/.test(l));
  assert.ok(ready >= 0, r.text);
  assert.strictEqual(out[ready + 1], `LOCATION whep ${loc}`, r.text);
  assert.ok(!out.some((l) => l.startsWith('LOCATION flv')), r.text);
});

test('run publishes, then plays, samples, stops players first and writes the stats', async () => {
  const dir = tmpDir();
  try {
    const out = path.join(dir, 'stats.json');
    const r = await runFake(['--mode', 'whip', '--url', WHIP, '--mode', 'whep', '--url', WHEP, '--seconds', '2', '--out', out, '--log-dir', dir], {
      whip: { readyMs: 30, rate: 20, location: 'http://127.0.0.1:1985/rtc/v1/whip/?action=delete' },
      whep: { readyMs: 20, rate: 20 },
    });
    assert.strictEqual(r.code, 0, r.text);
    assert.match(r.text, /^PASS whip ready connected in \d+ms$/m);
    assert.match(r.text, /^PASS whep ready playing in \d+ms$/m);
    assert.match(r.text, /^PASS whip framesEncoded \+\d+ in \d+\.\ds$/m);
    assert.match(r.text, /^PASS whep framesDecoded \+\d+ in \d+\.\ds$/m);
    assert.doesNotMatch(r.text, /FAIL/);

    // The player opens only after the publisher is ready, and stops first.
    const gotoWhip = r.fake.log.find((e) => e.what === 'goto' && e.mode === 'whip');
    const gotoWhep = r.fake.log.find((e) => e.what === 'goto' && e.mode === 'whep');
    assert.ok(gotoWhep.at - gotoWhip.at >= 30, JSON.stringify(r.fake.log));
    const stops = r.fake.log.filter((e) => e.what === 'stop').map((e) => e.mode);
    assert.deepStrictEqual(stops, ['whep', 'whip']);

    assert.strictEqual(r.fake.state.options.pipe, true);
    assert.ok(!r.fake.state.options.args.includes('--allow-loopback-in-peer-connection'));
    assert.strictEqual(path.dirname(r.fake.state.options.userDataDir), dir);
    assert.strictEqual(r.fake.state.closed, true);
    assert.strictEqual(r.fake.state.killed, null);

    const stats = JSON.parse(fs.readFileSync(out, 'utf8'));
    assert.strictEqual(stats.browser, 'Chrome/156.0.0.0');
    assert.strictEqual(stats.exitCode, 0);
    assert.strictEqual(stats.seconds, 2);
    assert.deepStrictEqual(stats.jobs.map((j) => [j.name, j.mode, j.result]), [['whip', 'whip', 'pass'], ['whep', 'whep', 'pass']]);
    const whip = stats.jobs[0];
    assert.strictEqual(whip.url, WHIP);
    assert.strictEqual(whip.location, 'http://127.0.0.1:1985/rtc/v1/whip/?action=delete');
    assert.ok(whip.readyMs >= 30, String(whip.readyMs));
    assert.ok(whip.samples.length >= 1);
    assert.ok(stats.jobs[1].samples.at(-1).inboundVideo.framesDecoded > 0);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('run opens each job in its own window', async () => {
  const r = await runFake(['--mode', 'flv', '--url', 'http://127.0.0.1:8080/live/s.flv', '--mode', 'hls', '--url', 'http://127.0.0.1:8080/live/s.m3u8', '--seconds', '1'], {
    flv: { readyMs: 5, rate: 1 },
    hls: { readyMs: 5, rate: 1 },
  });
  assert.strictEqual(r.code, 0, r.text);
  assert.deepStrictEqual(r.fake.log.filter((e) => e.what === 'newPage').map((e) => e.options), [{ type: 'window' }, { type: 'window' }]);
  assert.match(r.text, /^PASS flv currentTime \+[\d.]+ in \d+\.\ds$/m);
  assert.match(r.text, /^PASS hls currentTime/m);
});

test('run gives the WHIP codec to the page and removes its temporary profile', async () => {
  const r = await runFake(['--mode', 'whip', '--url', WHIP, '--codec', 'av1', '--seconds', '1'], { whip: { readyMs: 5, rate: 20 } });
  assert.strictEqual(r.code, 0, r.text);
  const go = r.fake.log.find((e) => e.what === 'goto');
  assert.strictEqual(go.codec, 'av1');
  assert.strictEqual(go.url, WHIP);
  assert.strictEqual(r.fake.state.profileExisted, true);
  assert.strictEqual(fs.existsSync(r.fake.state.options.userDataDir), false);
});

test('run fails a job that is not ready in time, and still writes the stats', async () => {
  const dir = tmpDir();
  try {
    const out = path.join(dir, 's.json');
    const r = await runFake(['--mode', 'whep', '--url', WHEP, '--timeout', '1', '--seconds', '1', '--out', out], { whep: {} },
      { deps: { msPerSecond: 50 } });
    assert.strictEqual(r.code, 1, r.text);
    assert.match(r.text, /^FAIL whep: not ready in 1s, state signaling$/m);
    const stats = JSON.parse(fs.readFileSync(out, 'utf8'));
    assert.strictEqual(stats.exitCode, 1);
    assert.strictEqual(stats.jobs[0].result, 'fail');
    assert.match(stats.jobs[0].reason, /not ready/);
    assert.strictEqual(r.fake.state.closed, true);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('run reports the page error of a job', async () => {
  const r = await runFake(['--mode', 'whep', '--url', WHEP, '--seconds', '1'], {
    whep: { error: `WHEP ${WHEP} answered HTTP 500: stream not found`, errorMs: 5 },
  });
  assert.strictEqual(r.code, 1, r.text);
  assert.match(r.text, /^FAIL whep: WHEP .* answered HTTP 500: stream not found$/m);
});

test('run fails a job whose test page does not load', async () => {
  const r = await runFake(['--mode', 'whep', '--url', WHEP, '--seconds', '1'], { whep: { gotoError: 'Navigation timeout of 10000 ms exceeded' } });
  assert.strictEqual(r.code, 1, r.text);
  assert.match(r.text, /^FAIL whep: the test page did not load: Navigation timeout/m);
  assert.strictEqual(r.fake.state.closed, true);
});

test('run fails a job whose stream does not move', async () => {
  const r = await runFake(['--mode', 'whep', '--url', WHEP, '--seconds', '2'], { whep: { readyMs: 5, rate: 0 } });
  assert.strictEqual(r.code, 1, r.text);
  assert.match(r.text, /^PASS whep ready playing/m);
  assert.match(r.text, /^FAIL whep: framesDecoded did not rise in \d+\.\ds: 0 to 0$/m);
});

test('run exits 6 when the page says SRS did not answer', async () => {
  const r = await runFake(['--mode', 'whep', '--url', WHEP, '--seconds', '1'], {
    whep: { error: `WHEP ${WHEP} did not answer: Failed to fetch`, errorMs: 5 },
  });
  assert.strictEqual(r.code, 6, r.text);
  assert.match(r.text, /^FAIL whep: WHEP .*did not answer/m);
});

test('run exits 6 without a browser when a job server does not listen', async () => {
  const targets = [];
  const r = await runFake(['--mode', 'whep', '--url', WHEP, '--mode', 'flv', '--url', 'http://127.0.0.1:8080/live/s.flv'], { whep: { readyMs: 1 }, flv: { readyMs: 1 } }, {
    deps: {
      connect: async (host, port) => {
        targets.push(`${host}:${port}`);
        return port === 1985 ? 'ECONNREFUSED' : null;
      },
    },
  });
  assert.strictEqual(r.code, 6, r.text);
  assert.match(r.text, /^FAIL whep: SRS did not answer at 127\.0\.0\.1:1985: ECONNREFUSED$/m);
  assert.doesNotMatch(r.text, /FAIL flv/);
  assert.deepStrictEqual(targets.sort(), ['127.0.0.1:1985', '127.0.0.1:8080']);
  assert.strictEqual(r.fake.state.launched, false);
});

test('run skips a codec the browser lacks, and the players of that stream', async () => {
  const whip265 = 'http://127.0.0.1:1985/rtc/v1/whip/?app=live&stream=s265';
  const whep265 = 'http://127.0.0.1:1985/rtc/v1/whep/?app=live&stream=s265';
  const r = await runFake([
    '--mode', 'whip', '--url', WHIP, '--codec', 'h264',
    '--mode', 'whip', '--url', whip265, '--codec', 'h265',
    '--mode', 'whep', '--url', WHEP, '--name', 'play-h264',
    '--mode', 'whep', '--url', whep265, '--name', 'play-h265',
    '--seconds', '1',
  ], {
    [`whip ${WHIP}`]: { readyMs: 5, rate: 20 },
    [`whip ${whip265}`]: { error: "codec h265 is not in the browser's send capabilities", errorState: 'unsupported', errorMs: 1 },
    [`whep ${WHEP}`]: { readyMs: 5, rate: 20 },
  });
  assert.strictEqual(r.code, 0, r.text);
  assert.match(r.text, /^SKIP whip-h265: codec h265 is not in the browser's send capabilities$/m);
  assert.match(r.text, /^SKIP play-h265: publisher whip-h265 skipped$/m);
  assert.match(r.text, /^PASS play-h264 framesDecoded/m);
  assert.ok(!r.fake.log.some((e) => e.what === 'goto' && e.url === whep265), JSON.stringify(r.fake.log));
});

test('run gives the players of an H.265 publisher the H.265 ready timeout', async () => {
  const whip265 = 'http://127.0.0.1:1985/rtc/v1/whip/?app=live&stream=s265';
  const whep265 = 'http://127.0.0.1:1985/rtc/v1/whep/?app=live&stream=s265';
  const r = await runFake([
    '--mode', 'whip', '--url', WHIP, '--codec', 'h264',
    '--mode', 'whip', '--url', whip265, '--codec', 'h265',
    '--mode', 'whep', '--url', WHEP, '--name', 'play-h264',
    '--mode', 'whep', '--url', whep265, '--name', 'play-h265',
    '--seconds', '1',
  ], {
    [`whip ${WHIP}`]: { readyMs: 5, rate: 20 },
    [`whip ${whip265}`]: { readyMs: 5, rate: 20 },
    [`whep ${WHEP}`]: { readyMs: 700, rate: 20 },
    [`whep ${whep265}`]: { readyMs: 700, rate: 20 },
  });
  assert.match(r.text, /^FAIL play-h264: not ready in 10s, state signaling$/m);
  assert.match(r.text, /^PASS play-h265 ready playing in \d+ms$/m);
});

test('run does not open the players when a publisher fails', async () => {
  const r = await runFake(['--mode', 'whip', '--url', WHIP, '--mode', 'whep', '--url', WHEP, '--seconds', '1'], {
    whip: { error: `WHIP ${WHIP} answered HTTP 400: 5018 RtcSdpNegotiate`, errorMs: 1 },
    whep: { readyMs: 1, rate: 20 },
  });
  assert.strictEqual(r.code, 1, r.text);
  assert.match(r.text, /^FAIL whip: WHIP .*HTTP 400/m);
  assert.match(r.text, /^FAIL whep: not started, publisher whip failed$/m);
  assert.ok(!r.fake.log.some((e) => e.what === 'goto' && e.mode === 'whep'));
});

test('run exits 1 with a FAIL line when the browser does not start', async () => {
  const r = await runFake(['--mode', 'whep', '--url', WHEP], {}, { launchError: new Error('Failed to launch the browser process') });
  assert.strictEqual(r.code, 1, r.text);
  assert.match(r.text, /^FAIL launch .*Failed to launch/m);
  assert.strictEqual(fs.existsSync(r.fake.state.options.userDataDir), false);
});

test('run exits 2 with a SKIP line without a browser, and 3 on bad arguments', async () => {
  const c = capture();
  const code = await chrome.main(['run', '--mode', 'whep', '--url', WHEP], { ...BASE_DEPS, exists: () => false, out: c.out, err: c.err });
  assert.strictEqual(code, 2);
  assert.match(c.lines.err.join('\n'), /^SKIP /m);
  const d = capture();
  assert.strictEqual(await chrome.main(['run', '--mode', 'whep'], { ...BASE_DEPS, out: d.out, err: d.err }), 3);
  assert.match(d.lines.err.join('\n'), /usage/i);
});

test('the default port check tells a listening server from a closed port', async () => {
  const server = net.createServer();
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  try {
    assert.strictEqual(await chrome.connectCheck('127.0.0.1', port, 2000), null);
  } finally {
    await new Promise((resolve) => server.close(resolve));
  }
  assert.match(await chrome.connectCheck('127.0.0.1', port, 2000), /ECONNREFUSED/);
});

const DUMP = { PeerConnections: { '9-1': { url: 'http://127.0.0.1/', stats: {} }, '9-2': { url: 'http://127.0.0.1/', stats: {} } }, UserAgent: 'Chrome/156' };

test('decodeInternalsDump reads a gzip or a plain dump and counts its peer connections', () => {
  const text = JSON.stringify(DUMP);
  for (const buf of [require('node:zlib').gzipSync(text), Buffer.from(text)]) {
    const d = chrome.decodeInternalsDump(buf);
    assert.strictEqual(d.text, text);
    assert.strictEqual(d.peerConnections, 2);
  }
  assert.throws(() => chrome.decodeInternalsDump(Buffer.from('<html>')), /not JSON/);
  assert.throws(() => chrome.decodeInternalsDump(Buffer.from('{"UserAgent":"x"}')), /PeerConnections/);
});

test('run --internals opens webrtc-internals first and saves its dump before the jobs stop', async () => {
  const dir = tmpDir();
  try {
    const file = path.join(dir, 'sub', 'internals.json');
    const out = path.join(dir, 'stats.json');
    const r = await runFake(['--mode', 'whip', '--url', WHIP, '--mode', 'whep', '--url', WHEP, '--seconds', '1', '--internals', file, '--out', out], {
      whip: { readyMs: 5, rate: 20 },
      whep: { readyMs: 5, rate: 20 },
    }, { internals: { dump: require('node:zlib').gzipSync(JSON.stringify(DUMP)), peers: 2 } });
    assert.strictEqual(r.code, 0, r.text);
    assert.ok(r.c.lines.out.includes(`PASS internals ${file}: peer connections 2`), r.text);
    assert.deepStrictEqual(JSON.parse(fs.readFileSync(file, 'utf8')), DUMP);

    const gotos = r.fake.log.filter((e) => e.what === 'goto').map((e) => e.mode);
    assert.deepStrictEqual(gotos, ['internals', 'whip', 'whep']);
    const dl = r.fake.log.find((e) => e.what === 'cdp' && e.method === 'Browser.setDownloadBehavior');
    assert.strictEqual(dl.params.behavior, 'allow');
    assert.strictEqual(dl.params.eventsEnabled, true);
    // The download lands in the profile, so it goes away with it.
    assert.strictEqual(path.dirname(dl.params.downloadPath), r.fake.state.options.userDataDir);
    assert.strictEqual(fs.existsSync(r.fake.state.options.userDataDir), false);
    // Chrome drops a closed peer connection from webrtc-internals.
    const stops = r.fake.log.map((e, i) => [e, i]).filter(([e]) => e.what === 'stop').map(([, i]) => i);
    const dump = r.fake.log.findIndex((e) => e.what === 'dump');
    assert.ok(dump >= 0 && dump < Math.min(...stops), JSON.stringify(r.fake.log));
    // It learns of a peer connection late, so the dump waits for both.
    const peers = r.fake.log.filter((e) => e.what === 'peers').map((e) => e.known);
    assert.deepStrictEqual(peers, [0, 1, 2]);
    assert.ok(r.fake.log.findLastIndex((e) => e.what === 'peers') < dump);

    const stats = JSON.parse(fs.readFileSync(out, 'utf8'));
    assert.deepStrictEqual(stats.internals, { file, peerConnections: 2, error: null });
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('run --internals fails the run when the dump does not come, and keeps the job results', async () => {
  const cases = [
    [{ complete: false }, /^FAIL internals: the dump did not download in \d+ms$/m],
    [{ noButton: true }, /^FAIL internals: no dump button on chrome:\/\/webrtc-internals$/m],
    [{ dump: Buffer.from('not a dump') }, /^FAIL internals: the dump is not JSON/m],
    [{ dump: Buffer.from('{"PeerConnections":{}}'), peers: 0 }, /^FAIL internals: the dump has 0 of 1 peer connections$/m, true],
  ];
  for (const [conf, re, saved] of cases) {
    const dir = tmpDir();
    try {
      const file = path.join(dir, 'internals.json');
      const r = await runFake(['--mode', 'whep', '--url', WHEP, '--seconds', '1', '--internals', file], { whep: { readyMs: 5, rate: 20 } },
        { internals: conf, deps: { internalsTimeoutMs: 200 } });
      assert.strictEqual(r.code, 1, r.text);
      assert.match(r.text, re);
      assert.match(r.text, /^PASS whep framesDecoded/m);
      // A short dump is still saved, for whoever reads the failure.
      assert.strictEqual(fs.existsSync(file), !!saved);
      assert.strictEqual(r.fake.state.closed, true);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  }
});

// The real browser and page against a stub that refuses every request.
test('the command line run answers a clear error from a stub server and a closed port', async (t) => {
  const executablePath = chrome.findBrowser({ platform: process.platform, env: process.env, exists: chrome.isExecutable });
  let puppeteer = null;
  try { puppeteer = require('puppeteer-core'); } catch { /* skipped below */ }
  if (!executablePath || !puppeteer) {
    t.skip('no browser or puppeteer-core');
    return;
  }
  const stub = http.createServer((req, res) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Headers', '*');
    if (req.method === 'OPTIONS') { res.writeHead(200); res.end(); return; }
    req.resume();
    req.on('end', () => { res.writeHead(500, { 'Content-Type': 'text/plain' }); res.end('stub: stream not found'); });
  });
  await new Promise((resolve) => stub.listen(0, '127.0.0.1', resolve));
  const port = stub.address().port;
  const dir = tmpDir();
  try {
    const out = path.join(dir, 'stats.json');
    const internals = path.join(dir, 'internals.json');
    const url = `http://127.0.0.1:${port}/rtc/v1/whep/?app=live&stream=s`;
    const child = require('node:child_process').spawn(process.execPath, [SCRIPT, 'run', '--mode', 'whep', '--url', url, '--seconds', '1', '--out', out, '--log-dir', dir,
      '--internals', internals],
      { stdio: ['ignore', 'pipe', 'pipe'] });
    let text = '';
    child.stdout.on('data', (d) => { text += d; });
    child.stderr.on('data', (d) => { text += d; });
    const code = await new Promise((resolve) => child.on('exit', resolve));
    assert.strictEqual(code, 1, text);
    assert.match(text, /^FAIL whep: WHEP .* answered HTTP 500: stub: stream not found$/m);
    const stats = JSON.parse(fs.readFileSync(out, 'utf8'));
    assert.strictEqual(stats.jobs[0].state, 'error');
    assert.match(stats.browser, /\//);
    // Headless Chrome saves the webrtc-internals dump of the page's
    // peer connection, which signaling failed.
    assert.match(text, /^PASS internals .*internals\.json: peer connections 1$/m);
    const dump = JSON.parse(fs.readFileSync(internals, 'utf8'));
    const pcs = Object.values(dump.PeerConnections);
    assert.strictEqual(pcs.length, 1);
    assert.match(pcs[0].url, /mode=whep/);
    assert.ok(pcs[0].updateLog.some((u) => u.type === 'createOffer'), JSON.stringify(pcs[0].updateLog.map((u) => u.type)));
  } finally {
    await new Promise((resolve) => stub.close(resolve));
    fs.rmSync(dir, { recursive: true, force: true });
  }

  const r = spawnSync(process.execPath, [SCRIPT, 'run', '--mode', 'whep', '--url', `http://127.0.0.1:${port}/rtc/v1/whep/?app=live&stream=s`],
    { encoding: 'utf8', timeout: 30000 });
  assert.strictEqual(r.status, 6, r.stdout + r.stderr);
  assert.match(r.stdout, /^FAIL whep: SRS did not answer at 127\.0\.0\.1:\d+: ECONNREFUSED$/m);
});
