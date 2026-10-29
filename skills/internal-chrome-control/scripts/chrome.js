#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const http = require('node:http');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const zlib = require('node:zlib');

const page = require('../page/srs-test.js');

const EXIT_PASS = 0;
const EXIT_FAIL = 1;
const EXIT_SKIP = 2;
const EXIT_USAGE = 3;
const EXIT_UNREACHABLE = 6;

const LAUNCH_TIMEOUT_MS = 20000;
const CLOSE_TIMEOUT_MS = 5000;
const CONNECT_TIMEOUT_MS = 2000;
const POLL_MS = 100;
const DEFAULT_SECONDS = 5;
const MAX_CONSOLE_LINES = 100;
const INTERNALS_TIMEOUT_MS = 10000;
const INTERNALS_URL = 'chrome://webrtc-internals';

const USAGE = [
  'usage: chrome.js <command> [options]',
  '  find-browser                print the browser executable',
  '  launch --probe [--headed] [--log-dir <dir>]',
  '                              start the browser, print its version and video codecs, close it',
  '  serve --port <n> [--host <h>]',
  '                              serve the test page and the players until stopped; host 127.0.0.1',
  '  run --mode <m> --url <u> [--codec <c>] [--name <n>] [--mode ...]',
  '      [--seconds <n>] [--timeout <s>] [--out <file>] [--internals <file>] [--log-dir <dir>] [--page-port <n>] [--headed]',
  '      [--allow-loopback]',
  '                              publish or play in the browser, one window per job, and check the stream moves;',
  '                              modes whip, whep, flv, ts, hls; codecs h264, h265, vp9, av1 (whip and whep)',
  'exit: 0 pass, 1 fail, 2 skip (no browser or puppeteer-core), 3 bad arguments, 6 SRS did not answer',
].join('\n');

class UsageError extends Error {}

function parseArgs(argv) {
  const [command, ...rest] = argv;
  if (command === 'find-browser') {
    if (rest.length) throw new UsageError(`find-browser takes no options: ${rest.join(' ')}`);
    return { command };
  }
  if (command === 'launch') {
    const opts = { command, probe: false, headed: false, logDir: null };
    for (let i = 0; i < rest.length; i++) {
      const a = rest[i];
      if (a === '--probe') opts.probe = true;
      else if (a === '--headed') opts.headed = true;
      else if (a === '--log-dir') {
        if (i + 1 >= rest.length) throw new UsageError('--log-dir needs a directory');
        opts.logDir = rest[++i];
      } else throw new UsageError(`unknown option ${a}`);
    }
    if (!opts.probe) throw new UsageError('launch needs --probe');
    return opts;
  }
  if (command === 'serve') {
    const opts = { command, port: null, host: '127.0.0.1' };
    for (let i = 0; i < rest.length; i++) {
      const a = rest[i];
      if (a === '--port' || a === '--host') {
        if (i + 1 >= rest.length) throw new UsageError(`${a} needs a value`);
        const v = rest[++i];
        if (a === '--host') opts.host = v;
        else {
          if (!/^\d+$/.test(v) || Number(v) > 65535) throw new UsageError(`--port must be 0 to 65535: ${v}`);
          opts.port = Number(v);
        }
      } else throw new UsageError(`unknown option ${a}`);
    }
    if (opts.port === null) throw new UsageError('serve needs --port');
    return opts;
  }
  if (command === 'run') return parseRunArgs(rest);
  throw new UsageError(command ? `unknown command ${command}` : 'no command');
}

function positiveInt(name, v, max) {
  if (!/^\d+$/.test(v) || Number(v) < 1 || Number(v) > max) throw new UsageError(`${name} must be 1 to ${max}: ${v}`);
  return Number(v);
}

// Each --mode starts a job; --url, --codec and --name belong to the job
// before them. The other options are for the whole run.
function parseRunArgs(rest) {
  const opts = {
    command: 'run', jobs: [], seconds: DEFAULT_SECONDS, timeout: null, out: null,
    internals: null, logDir: null, pagePort: 0, headed: false, allowLoopback: false,
  };
  const raw = [];
  for (let i = 0; i < rest.length; i++) {
    const a = rest[i];
    if (a === '--headed') {
      opts.headed = true;
      continue;
    }
    if (a === '--allow-loopback') {
      opts.allowLoopback = true;
      continue;
    }
    if (!['--mode', '--url', '--codec', '--name', '--seconds', '--timeout', '--out', '--internals', '--log-dir', '--page-port'].includes(a)) {
      throw new UsageError(`unknown option ${a}`);
    }
    if (i + 1 >= rest.length) throw new UsageError(`${a} needs a value`);
    const v = rest[++i];
    if (a === '--mode') raw.push({ mode: v });
    else if (a === '--url' || a === '--codec' || a === '--name') {
      const job = raw[raw.length - 1];
      if (!job) throw new UsageError(`${a} comes after the --mode of its job`);
      const key = a.slice(2);
      if (job[key] !== undefined) throw new UsageError(`${a} given twice for one job`);
      job[key] = v;
    } else if (a === '--seconds') opts.seconds = positiveInt(a, v, 3600);
    else if (a === '--timeout') opts.timeout = positiveInt(a, v, 3600);
    else if (a === '--out') opts.out = v;
    else if (a === '--internals') opts.internals = v;
    else if (a === '--log-dir') opts.logDir = v;
    else if (a === '--page-port') {
      if (!/^\d+$/.test(v) || Number(v) > 65535) throw new UsageError(`--page-port must be 0 to 65535: ${v}`);
      opts.pagePort = Number(v);
    }
  }
  if (!raw.length) throw new UsageError('run needs at least one --mode with its --url');

  const names = new Set();
  for (const r of raw) {
    const q = new URLSearchParams({ mode: r.mode, url: r.url || '' });
    if (r.codec) q.set('codec', r.codec);
    let parsed;
    try {
      parsed = page.parseOptions(`?${q}`);
    } catch (e) {
      throw new UsageError(e.message);
    }
    if (r.name !== undefined) {
      if (!/^[A-Za-z0-9._-]+$/.test(r.name)) throw new UsageError(`--name takes letters, digits, '.', '_' and '-': ${r.name}`);
      if (names.has(r.name)) throw new UsageError(`name ${r.name} is used twice`);
      names.add(r.name);
    }
    opts.jobs.push({ name: r.name, mode: parsed.mode, url: parsed.url, codec: parsed.codec });
  }
  // A job without --name is named after its mode, plus its codec or its
  // number when the mode repeats.
  const unnamed = opts.jobs.filter((j) => j.name === undefined);
  const named = unnamed.map((j) => {
    const same = unnamed.filter((x) => x.mode === j.mode);
    if (same.length === 1) return j.mode;
    if (j.codec && same.filter((x) => x.codec === j.codec).length === 1) return `${j.mode}-${j.codec}`;
    return `${j.mode}-${same.indexOf(j) + 1}`;
  });
  unnamed.forEach((j, i) => {
    if (names.has(named[i])) throw new UsageError(`name ${named[i]} is used twice; give the job a --name`);
    j.name = named[i];
  });
  return opts;
}

const CONTENT_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.map': 'application/json',
  '.json': 'application/json',
  '.css': 'text/css',
};

// The page next to this script, and the players vendored in the SRS tree.
function defaultServeDirs() {
  const skill = path.dirname(__dirname);
  return {
    pageDir: path.join(skill, 'page'),
    playersDir: path.resolve(skill, '..', '..', 'trunk', 'research', 'players', 'js'),
  };
}

function resolveInside(root, rel) {
  const file = path.resolve(root, rel);
  return file.startsWith(root + path.sep) ? file : null;
}

// Static files only: / is the page, /players/ the vendored players.
function createPageServer({ pageDir, playersDir }) {
  const roots = { page: path.resolve(pageDir), players: path.resolve(playersDir) };
  return http.createServer((req, res) => {
    const reply = (status, text) => {
      res.writeHead(status, { 'Content-Type': 'text/plain', 'Cache-Control': 'no-store' });
      res.end(req.method === 'HEAD' ? undefined : text);
    };
    if (req.method !== 'GET' && req.method !== 'HEAD') return reply(405, 'method not allowed\n');
    let pathname;
    try {
      pathname = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    } catch {
      return reply(400, 'bad path\n');
    }
    if (pathname.includes('\0')) return reply(400, 'bad path\n');
    let file;
    if (pathname === '/') file = path.join(roots.page, 'index.html');
    else if (pathname.startsWith('/players/')) file = resolveInside(roots.players, pathname.slice('/players/'.length));
    else file = resolveInside(roots.page, pathname.slice(1));
    let stat = null;
    try { stat = file && fs.statSync(file); } catch { stat = null; }
    if (!stat || !stat.isFile()) return reply(404, 'not found\n');
    res.writeHead(200, {
      'Content-Type': CONTENT_TYPES[path.extname(file)] || 'application/octet-stream',
      'Content-Length': stat.size,
      'Cache-Control': 'no-store',
    });
    if (req.method === 'HEAD') return res.end();
    fs.createReadStream(file).pipe(res);
  });
}

function serve(opts, deps) {
  const dirs = defaultServeDirs();
  const server = createPageServer(dirs);
  return new Promise((resolve) => {
    server.once('error', (e) => {
      deps.out(`FAIL serve ${opts.host}:${opts.port}: ${e.code || e.message}`);
      resolve(EXIT_FAIL);
    });
    server.listen(opts.port, opts.host, () => {
      const port = server.address().port;
      deps.out(`serving http://${opts.host}:${port}/ page ${dirs.pageDir} players ${dirs.playersDir}`);
      const stop = () => server.close(() => resolve(EXIT_PASS));
      process.once('SIGINT', stop);
      process.once('SIGTERM', stop);
    });
  });
}

function browserCandidates(platform, env) {
  if (platform === 'darwin') {
    const apps = ['/Applications'];
    if (env.HOME) apps.push(path.posix.join(env.HOME, 'Applications'));
    const bundles = [
      'Google Chrome.app/Contents/MacOS/Google Chrome',
      'Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
      'Chromium.app/Contents/MacOS/Chromium',
    ];
    return bundles.flatMap((b) => apps.map((dir) => path.posix.join(dir, b)));
  }
  if (platform === 'win32') {
    const roots = [env.PROGRAMFILES, env['PROGRAMFILES(X86)'], env.LOCALAPPDATA].filter(Boolean);
    const installs = [
      'Google\\Chrome\\Application\\chrome.exe',
      'Microsoft\\Edge\\Application\\msedge.exe',
      'Chromium\\Application\\chrome.exe',
    ];
    return installs.flatMap((i) => roots.map((root) => path.win32.join(root, i)));
  }
  return [
    '/usr/bin/google-chrome-stable',
    '/usr/bin/google-chrome',
    '/opt/google/chrome/chrome',
    '/usr/bin/microsoft-edge-stable',
    '/usr/bin/microsoft-edge',
    '/opt/microsoft/msedge/msedge',
    '/usr/bin/chromium',
    '/usr/bin/chromium-browser',
    '/snap/bin/chromium',
  ];
}

function isExecutable(p) {
  try {
    fs.accessSync(p, fs.constants.X_OK);
    return fs.statSync(p).isFile();
  } catch {
    return false;
  }
}

// CHROME_PATH is set on purpose, such as on CI; falling back to another
// browser would hide that it is wrong.
function findBrowser({ platform, env, exists }) {
  if (env.CHROME_PATH) {
    if (!exists(env.CHROME_PATH)) throw new UsageError(`CHROME_PATH is not an executable: ${env.CHROME_PATH}`);
    return env.CHROME_PATH;
  }
  return browserCandidates(platform, env).find(exists) || null;
}

// pipe: CDP over stdio, so the browser opens no debugging port and nothing
// else can attach to it. No loopback candidates unless asked: with both a
// loopback and a LAN pair SRS flips its send address between them and media
// stalls, so loopback is only for a host with no other IPv4.
// Several jobs run as windows side by side, so a window in the background
// must not pause its video or slow its timers.
function launchOptions({ executablePath, userDataDir, headed, allowLoopback }) {
  const extra = allowLoopback ? ['--allow-loopback-in-peer-connection'] : [];
  return {
    executablePath,
    userDataDir,
    pipe: true,
    headless: !headed,
    timeout: LAUNCH_TIMEOUT_MS,
    args: [
      '--use-fake-device-for-media-stream',
      '--use-fake-ui-for-media-stream',
      '--autoplay-policy=no-user-gesture-required',
      '--disable-background-media-suspend',
      '--disable-renderer-backgrounding',
      '--disable-background-timer-throttling',
      '--disable-backgrounding-occluded-windows',
      '--no-first-run',
      '--no-default-browser-check',
      ...extra,
    ],
  };
}

function loadPuppeteer() {
  try {
    return require('puppeteer-core');
  } catch {
    return null;
  }
}

async function closeBrowser(browser, timeoutMs) {
  let timer;
  const timedOut = new Promise((resolve) => { timer = setTimeout(() => resolve(true), timeoutMs); });
  const closed = browser.close().then(() => false, () => false);
  const hung = await Promise.race([closed, timedOut]);
  clearTimeout(timer);
  if (hung) {
    const proc = browser.process();
    if (proc) proc.kill('SIGKILL');
  }
  return hung;
}

async function probe(opts, deps, executablePath) {
  const puppeteer = deps.puppeteer || loadPuppeteer();
  if (!puppeteer) {
    deps.err(`SKIP puppeteer-core is not installed; run npm ci in ${path.dirname(__dirname)}`);
    return EXIT_SKIP;
  }

  let userDataDir;
  let keepProfile = false;
  if (opts.logDir) {
    fs.mkdirSync(opts.logDir, { recursive: true });
    userDataDir = fs.mkdtempSync(path.join(opts.logDir, 'chrome-profile-'));
    keepProfile = true;
  } else {
    userDataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'chrome-control-'));
  }

  const started = Date.now();
  let browser = null;
  try {
    browser = await puppeteer.launch(launchOptions({ executablePath, userDataDir, headed: opts.headed }));
    const ms = Date.now() - started;
    const version = await browser.version();
    const pid = browser.process() ? browser.process().pid : 'unknown';
    const page = await browser.newPage();
    const codecs = await page.evaluate(() => [...new Set(RTCRtpSender.getCapabilities('video').codecs.map((c) => c.mimeType))]);
    deps.out(`PASS launch ${version} pid ${pid} in ${ms}ms: ${executablePath}`);
    deps.out(`video codecs: ${codecs.join(' ')}`);
    return EXIT_PASS;
  } catch (e) {
    deps.out(`FAIL launch ${executablePath}: ${e.message}`);
    return EXIT_FAIL;
  } finally {
    if (browser) {
      const hung = await closeBrowser(browser, deps.closeTimeoutMs || CLOSE_TIMEOUT_MS);
      if (hung) deps.err(`browser did not close in time; killed pid ${browser.process() && browser.process().pid}`);
    }
    removeProfile({ dir: userDataDir, keep: keepProfile }, deps.err);
  }
}

// A temporary profile removed at exit, or one kept under the log folder.
function makeProfile(logDir) {
  if (logDir) {
    fs.mkdirSync(logDir, { recursive: true });
    return { dir: fs.mkdtempSync(path.join(logDir, 'chrome-profile-')), keep: true };
  }
  return { dir: fs.mkdtempSync(path.join(os.tmpdir(), 'chrome-control-')), keep: false };
}

// On Windows under load, the browser's helper processes can hold the profile
// for seconds after close: a profile left behind is a warning, not a failure.
function removeProfile(profile, warn, rm = fs.rmSync) {
  if (profile.keep) return true;
  try {
    rm(profile.dir, { recursive: true, force: true, maxRetries: 15, retryDelay: 200 });
    return true;
  } catch (e) {
    warn(`profile ${profile.dir} not removed: ${e.message}`);
    return false;
  }
}

// WHIP and WHEP name the stream in the query, HTTP streams in the path.
function streamKey(url) {
  const u = new URL(url);
  const stream = u.searchParams.get('stream');
  if (stream) return `${u.searchParams.get('app') || 'live'}/${stream}`;
  return u.pathname.replace(/^\/+/, '').replace(/\.(flv|ts|m3u8)$/, '');
}

function readyStates(mode) {
  return mode === 'whip' ? ['connected'] : ['playing'];
}

// A player of an H.265 stream waits for a hardware encoder's first keyframe,
// which takes seconds.
function readyTimeoutMs(job, timeout, msPerSecond = 1000) {
  return (timeout || ((job.codec || job.streamCodec) === 'h265' ? 20 : 10)) * msPerSecond;
}

const PROGRESS = {
  whip: { metric: 'framesEncoded', read: (s) => (s.outboundVideo ? s.outboundVideo.framesEncoded : null) },
  whep: { metric: 'framesDecoded', read: (s) => (s.inboundVideo ? s.inboundVideo.framesDecoded : null) },
  http: { metric: 'currentTime', read: (s) => (s.media ? s.media.currentTime : null) },
};

// How far the stream moved from the last sample before it was ready to the
// last sample, which stop() takes. Nothing counted before ready is 0.
function progress(job) {
  const p = PROGRESS[job.mode] || PROGRESS.http;
  const ready = job.events.find((e) => readyStates(job.mode).includes(e.name));
  const last = job.samples[job.samples.length - 1];
  if (!ready || !last || p.read(last) === null || p.read(last) === undefined) return null;
  const before = job.samples.filter((s) => s.t <= ready.t).pop();
  const from = before && typeof p.read(before) === 'number' ? p.read(before) : 0;
  return { metric: p.metric, from, to: p.read(last), ms: last.t - ready.t };
}

function exitCode(jobs) {
  if (jobs.some((j) => j.unreachable)) return EXIT_UNREACHABLE;
  if (jobs.some((j) => j.result === 'fail')) return EXIT_FAIL;
  return EXIT_PASS;
}

// null when something accepts a TCP connection at host:port, else the error.
function connectCheck(host, port, timeoutMs = CONNECT_TIMEOUT_MS) {
  return new Promise((resolve) => {
    const sock = net.connect({ host, port });
    const done = (err) => {
      sock.destroy();
      resolve(err);
    };
    sock.setTimeout(timeoutMs, () => done(`no answer in ${timeoutMs}ms`));
    sock.once('connect', () => done(null));
    sock.once('error', (e) => done(e.code || e.message));
  });
}

function hostPort(url) {
  const u = new URL(url);
  return { host: u.hostname.replace(/^\[|\]$/g, ''), port: Number(u.port) || (u.protocol === 'https:' ? 443 : 80) };
}

const delay = (ms) => new Promise((resolve) => { setTimeout(resolve, ms); });

function withTimeout(promise, ms, message) {
  let timer;
  const late = new Promise((_, reject) => { timer = setTimeout(() => reject(new Error(`${message} in ${ms}ms`)), ms); });
  return Promise.race([promise, late]).finally(() => clearTimeout(timer));
}

// Read in the page: the job's state, and everything but stop().
const readState = () => (window.srsTest
  ? { state: window.srsTest.state, error: window.srsTest.error, location: window.srsTest.location } : null);
const readResult = () => {
  const t = window.srsTest;
  if (!t) return null;
  return { state: t.state, error: t.error, location: t.location, events: t.events, samples: t.samples };
};
const stopJob = () => (window.srsTest ? window.srsTest.stop() : null);

// Chrome 156 downloads the webrtc-internals dump as gzip JSON; plain JSON is read too.
function decodeInternalsDump(buf) {
  const text = buf[0] === 0x1f && buf[1] === 0x8b ? zlib.gunzipSync(buf).toString('utf8') : buf.toString('utf8');
  let dump;
  try {
    dump = JSON.parse(text);
  } catch (e) {
    throw new Error(`the dump is not JSON: ${e.message}`);
  }
  if (!dump || typeof dump.PeerConnections !== 'object' || dump.PeerConnections === null) {
    throw new Error('the dump has no PeerConnections');
  }
  return { text, peerConnections: Object.keys(dump.PeerConnections).length };
}

// null when the page has no store to read, such as in another Chrome version.
const countPeers = () => {
  const store = window.peerConnectionDataStore;
  return store ? Object.keys(store).length : null;
};

const clickDump = () => {
  const button = document.querySelector('#dump-click-target');
  if (!button) return false;
  button.click();
  return true;
};

// The webrtc-internals window opens before the jobs, so it sees every peer
// connection from its start. Its dump button downloads into a folder in the
// profile, which goes away with the profile.
async function openInternals(browser, profileDir) {
  const internals = { dir: fs.mkdtempSync(path.join(profileDir, 'internals-')), done: null };
  internals.tab = await browser.newPage({ type: 'window' });
  await internals.tab.goto(INTERNALS_URL, { waitUntil: 'load' });
  const cdp = await internals.tab.createCDPSession();
  cdp.on('Browser.downloadProgress', (e) => {
    if (e.state === 'completed' || e.state === 'canceled') internals.done = e.state;
  });
  await cdp.send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: internals.dir, eventsEnabled: true });
  return internals;
}

// The page learns of a new peer connection a few hundred ms late, so wait
// until it knows as many as the WebRTC jobs opened. A short dump is saved
// and still fails.
async function saveInternals(internals, file, expected, timeoutMs, pollMs) {
  let deadline = Date.now() + timeoutMs;
  for (;;) {
    const known = await internals.tab.evaluate(countPeers);
    if (known === null || known >= expected || Date.now() >= deadline) break;
    await delay(pollMs);
  }
  if (!await internals.tab.evaluate(clickDump)) throw new Error(`no dump button on ${INTERNALS_URL}`);
  deadline = Date.now() + timeoutMs;
  while (!internals.done) {
    if (Date.now() >= deadline) throw new Error(`the dump did not download in ${timeoutMs}ms`);
    await delay(pollMs);
  }
  if (internals.done !== 'completed') throw new Error(`the dump download was ${internals.done}`);
  const files = fs.readdirSync(internals.dir);
  if (files.length !== 1) throw new Error(`expected one downloaded dump, found ${files.length}`);
  const dump = decodeInternalsDump(fs.readFileSync(path.join(internals.dir, files[0])));
  fs.mkdirSync(path.dirname(path.resolve(file)), { recursive: true });
  fs.writeFileSync(file, dump.text);
  if (dump.peerConnections < expected) throw new Error(`the dump has ${dump.peerConnections} of ${expected} peer connections`);
  return dump;
}

function listenPageServer(port) {
  const server = createPageServer(defaultServeDirs());
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, '127.0.0.1', () => resolve(server));
  });
}

// Open the jobs together, each in its own window: Chrome pauses a
// video-only <video> in a background tab. Then poll until each is ready,
// failed, or past its deadline.
async function openJobs(browser, jobs, ctx) {
  await Promise.all(jobs.map(async (job) => {
    const q = new URLSearchParams({ mode: job.mode, url: job.url });
    if (job.codec) q.set('codec', job.codec);
    job.tab = await browser.newPage({ type: 'window' });
    if (job.tab.on) {
      const note = (line) => { if (job.console.length < MAX_CONSOLE_LINES) job.console.push(line); };
      job.tab.on('console', (m) => note(`${m.type()} ${m.text()}`));
      job.tab.on('pageerror', (e) => note(`pageerror ${e.message}`));
    }
    job.openedAt = Date.now();
    try {
      await job.tab.goto(`${ctx.pageUrl}?${q}`, { waitUntil: 'load', timeout: readyTimeoutMs(job, ctx.opts.timeout, ctx.msPerSecond) });
    } catch (e) {
      job.result = 'fail';
      job.reason = `the test page did not load: ${e.message}`;
    }
  }));
  await Promise.all(jobs.filter((j) => !j.result).map(async (job) => {
    const deadline = job.openedAt + readyTimeoutMs(job, ctx.opts.timeout, ctx.msPerSecond);
    for (;;) {
      const st = await job.tab.evaluate(readState);
      const state = st ? st.state : 'loading';
      if (readyStates(job.mode).includes(state)) {
        job.readyMs = Date.now() - job.openedAt;
        ctx.deps.out(`PASS ${job.name} ready ${state} in ${job.readyMs}ms`);
        // The WHIP or WHEP resource names the SRS session, such as for the NACK simulator.
        if (st.location) ctx.deps.out(`LOCATION ${job.name} ${st.location}`);
        return;
      }
      if (state === 'unsupported') {
        job.result = 'skip';
        job.reason = st.error;
        return;
      }
      if (state === 'error' || state === 'stopped') {
        job.result = 'fail';
        job.reason = st.error || `stopped before ready`;
        if (/did not answer/.test(job.reason)) job.unreachable = true;
        return;
      }
      if (Date.now() >= deadline) {
        job.result = 'fail';
        job.reason = `not ready in ${readyTimeoutMs(job, ctx.opts.timeout) / 1000}s, state ${state}`;
        return;
      }
      await delay(ctx.pollMs);
    }
  }));
}

function checkProgress(job, out) {
  const p = progress(job);
  if (!p) {
    job.result = 'fail';
    job.reason = `no ${(PROGRESS[job.mode] || PROGRESS.http).metric} in the samples`;
    return;
  }
  const secs = (p.ms / 1000).toFixed(1);
  if (p.to > p.from) {
    job.result = 'pass';
    out(`PASS ${job.name} ${p.metric} +${+(p.to - p.from).toFixed(3)} in ${secs}s`);
  } else {
    job.result = 'fail';
    job.reason = `${p.metric} did not rise in ${secs}s: ${p.from} to ${p.to}`;
  }
}

function writeStats(file, report) {
  fs.mkdirSync(path.dirname(path.resolve(file)), { recursive: true });
  fs.writeFileSync(file, `${JSON.stringify(report, null, 2)}\n`);
}

// Publishers first; the players open once every publisher is ready, so a
// WHEP or HTTP job finds its stream. All jobs sample for --seconds, then
// the players stop before the publishers.
async function run(opts, deps, executablePath) {
  const puppeteer = deps.puppeteer || loadPuppeteer();
  if (!puppeteer) {
    deps.err(`SKIP puppeteer-core is not installed; run npm ci in ${path.dirname(__dirname)}`);
    return EXIT_SKIP;
  }
  const msPerSecond = deps.msPerSecond || 1000;
  const connect = deps.connect || connectCheck;
  const jobs = opts.jobs.map((j) => ({
    ...j, result: null, reason: null, unreachable: false, readyMs: null, console: [],
    state: null, error: null, location: null, events: [], samples: [],
  }));

  const targets = new Map();
  for (const job of jobs) {
    const { host, port } = hostPort(job.url);
    const key = `${host.includes(':') ? `[${host}]` : host}:${port}`;
    if (!targets.has(key)) targets.set(key, connect(host, port));
  }
  let unreachable = false;
  for (const job of jobs) {
    const { host, port } = hostPort(job.url);
    const key = `${host.includes(':') ? `[${host}]` : host}:${port}`;
    const err = await targets.get(key);
    if (err) {
      deps.out(`FAIL ${job.name}: SRS did not answer at ${key}: ${err}`);
      unreachable = true;
    }
  }
  if (unreachable) return EXIT_UNREACHABLE;

  const profile = makeProfile(opts.logDir);
  let server = null;
  let browser = null;
  let version = null;
  const report = { command: 'run', browser: null, executablePath, startedAt: new Date().toISOString(), seconds: opts.seconds, exitCode: null, jobs: [] };
  let internals = null;
  if (opts.internals) report.internals = { file: opts.internals, peerConnections: null, error: null };
  try {
    try {
      server = await listenPageServer(opts.pagePort);
    } catch (e) {
      deps.out(`FAIL page server 127.0.0.1:${opts.pagePort}: ${e.code || e.message}`);
      return EXIT_FAIL;
    }
    try {
      browser = await puppeteer.launch(launchOptions({
        executablePath, userDataDir: profile.dir, headed: opts.headed, allowLoopback: opts.allowLoopback,
      }));
      version = await browser.version();
    } catch (e) {
      deps.out(`FAIL launch ${executablePath}: ${e.message}`);
      return EXIT_FAIL;
    }
    report.browser = version;
    const ctx = {
      opts, deps, msPerSecond, pollMs: deps.pollMs || POLL_MS,
      pageUrl: `http://127.0.0.1:${server.address().port}/`,
    };

    if (opts.internals) {
      try {
        internals = await openInternals(browser, profile.dir);
      } catch (e) {
        report.internals.error = `${INTERNALS_URL} did not open: ${e.message}`;
      }
    }

    const publishers = jobs.filter((j) => j.mode === 'whip');
    const players = jobs.filter((j) => j.mode !== 'whip');
    await openJobs(browser, publishers, ctx);
    const open = [];
    for (const job of players) {
      const pub = publishers.find((p) => streamKey(p.url) === streamKey(job.url));
      if (pub) job.streamCodec = pub.codec;
      if (pub && pub.result === 'skip') {
        job.result = 'skip';
        job.reason = `publisher ${pub.name} skipped`;
      } else if (publishers.some((p) => p.result === 'fail')) {
        job.result = 'fail';
        job.reason = `not started, publisher ${publishers.find((p) => p.result === 'fail').name} failed`;
      } else open.push(job);
    }
    await openJobs(browser, open, ctx);

    if (jobs.some((j) => j.readyMs !== null)) await delay(opts.seconds * msPerSecond);
    // Before the stops: Chrome drops a closed peer connection from webrtc-internals.
    if (internals) {
      try {
        const expected = jobs.filter((j) => j.tab && (j.mode === 'whip' || j.mode === 'whep')).length;
        const dump = await saveInternals(internals, opts.internals, expected, deps.internalsTimeoutMs || INTERNALS_TIMEOUT_MS, ctx.pollMs);
        report.internals.peerConnections = dump.peerConnections;
        deps.out(`PASS internals ${opts.internals}: peer connections ${dump.peerConnections}`);
      } catch (e) {
        report.internals.error = e.message;
      }
    }
    if (report.internals && report.internals.error) deps.out(`FAIL internals: ${report.internals.error}`);
    for (const job of players.concat(publishers)) {
      if (!job.tab) continue;
      try {
        await withTimeout(job.tab.evaluate(stopJob), CLOSE_TIMEOUT_MS, 'stop did not finish');
        Object.assign(job, await withTimeout(job.tab.evaluate(readResult), CLOSE_TIMEOUT_MS, 'no result'));
      } catch (e) {
        job.console.push(`stop ${e.message}`);
        if (!job.result) {
          job.result = 'fail';
          job.reason = `could not stop the page: ${e.message}`;
        }
      }
    }
    for (const job of jobs) {
      if (job.result === null) checkProgress(job, deps.out);
    }
  } finally {
    for (const job of jobs) {
      if (job.result === 'fail') deps.out(`FAIL ${job.name}: ${job.reason}`);
      if (job.result === 'skip') deps.out(`SKIP ${job.name}: ${job.reason}`);
    }
    if (browser) {
      const hung = await closeBrowser(browser, deps.closeTimeoutMs || CLOSE_TIMEOUT_MS);
      if (hung) deps.err(`browser did not close in time; killed pid ${browser.process() && browser.process().pid}`);
    }
    if (server) await new Promise((resolve) => { server.close(resolve); });
    removeProfile(profile, deps.err);
  }

  let code = exitCode(jobs);
  if (code === EXIT_PASS && report.internals && report.internals.error) code = EXIT_FAIL;
  if (opts.out) {
    report.exitCode = code;
    report.jobs = jobs.map(({ tab, openedAt, unreachable: u, ...j }) => j);
    writeStats(opts.out, report);
    deps.out(`stats ${opts.out}`);
  }
  return code;
}

async function main(argv, deps = {}) {
  deps = {
    platform: process.platform,
    env: process.env,
    exists: isExecutable,
    out: (s) => process.stdout.write(`${s}\n`),
    err: (s) => process.stderr.write(`${s}\n`),
    ...deps,
  };
  try {
    const opts = parseArgs(argv);
    if (opts.command === 'serve') return await serve(opts, deps);
    const browser = findBrowser(deps);
    if (!browser) {
      deps.err('SKIP no Chrome, Edge or Chromium found; set CHROME_PATH to the browser executable');
      return EXIT_SKIP;
    }
    if (opts.command === 'find-browser') {
      deps.out(browser);
      return EXIT_PASS;
    }
    if (opts.command === 'run') return await run(opts, deps, browser);
    return await probe(opts, deps, browser);
  } catch (e) {
    if (e instanceof UsageError) {
      deps.err(`${e.message}\n${USAGE}`);
      return EXIT_USAGE;
    }
    throw e;
  }
}

module.exports = {
  UsageError,
  parseArgs,
  browserCandidates,
  findBrowser,
  isExecutable,
  launchOptions,
  streamKey,
  readyStates,
  readyTimeoutMs,
  progress,
  exitCode,
  connectCheck,
  decodeInternalsDump,
  defaultServeDirs,
  createPageServer,
  removeProfile,
  main,
};

if (require.main === module) {
  main(process.argv.slice(2)).then((code) => { process.exitCode = code; }, (e) => {
    process.stderr.write(`FAIL ${e.stack || e}\n`);
    process.exitCode = EXIT_FAIL;
  });
}
