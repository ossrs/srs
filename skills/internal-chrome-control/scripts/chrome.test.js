'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const chrome = require('./chrome.js');

const SCRIPT = path.join(__dirname, 'chrome.js');

function capture() {
  const lines = { out: [], err: [] };
  return {
    lines,
    out: (s) => lines.out.push(s),
    err: (s) => lines.err.push(s),
  };
}

function existsIn(list) {
  const set = new Set(list);
  return (p) => set.has(p);
}

test('parseArgs reads find-browser', () => {
  assert.deepStrictEqual(chrome.parseArgs(['find-browser']), { command: 'find-browser' });
});

test('parseArgs reads launch --probe with its options', () => {
  assert.deepStrictEqual(chrome.parseArgs(['launch', '--probe']), {
    command: 'launch', probe: true, headed: false, logDir: null,
  });
  assert.deepStrictEqual(chrome.parseArgs(['launch', '--probe', '--headed', '--log-dir', '/tmp/x']), {
    command: 'launch', probe: true, headed: true, logDir: '/tmp/x',
  });
});

test('parseArgs rejects what it does not know', () => {
  assert.throws(() => chrome.parseArgs([]), chrome.UsageError);
  assert.throws(() => chrome.parseArgs(['open']), chrome.UsageError);
  assert.throws(() => chrome.parseArgs(['find-browser', '--x']), chrome.UsageError);
  assert.throws(() => chrome.parseArgs(['launch']), /--probe/);
  assert.throws(() => chrome.parseArgs(['launch', '--probe', '--log-dir']), /--log-dir/);
  assert.throws(() => chrome.parseArgs(['launch', '--probe', '--bogus']), /--bogus/);
});

test('browser candidates follow CHROME_PATH, Chrome, Edge, Chromium on macOS', () => {
  const list = chrome.browserCandidates('darwin', { HOME: '/Users/u' });
  const chromeAt = list.indexOf('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome');
  const userChromeAt = list.indexOf('/Users/u/Applications/Google Chrome.app/Contents/MacOS/Google Chrome');
  const edgeAt = list.indexOf('/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge');
  const chromiumAt = list.indexOf('/Applications/Chromium.app/Contents/MacOS/Chromium');
  assert.ok(chromeAt >= 0 && userChromeAt >= 0 && edgeAt >= 0 && chromiumAt >= 0, list.join('\n'));
  assert.ok(Math.max(chromeAt, userChromeAt) < edgeAt && edgeAt < chromiumAt, list.join('\n'));
});

test('browser candidates on Windows use the Program Files and LocalAppData folders', () => {
  const env = {
    PROGRAMFILES: 'C:\\Program Files',
    'PROGRAMFILES(X86)': 'C:\\Program Files (x86)',
    LOCALAPPDATA: 'C:\\Users\\u\\AppData\\Local',
  };
  const list = chrome.browserCandidates('win32', env);
  const chromeAt = list.indexOf('C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe');
  const edgeAt = list.indexOf('C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe');
  const chromiumAt = list.indexOf('C:\\Users\\u\\AppData\\Local\\Chromium\\Application\\chrome.exe');
  assert.ok(chromeAt >= 0 && edgeAt >= 0 && chromiumAt >= 0, list.join('\n'));
  assert.ok(chromeAt < edgeAt && edgeAt < chromiumAt, list.join('\n'));
  assert.ok(list.every((p) => !p.includes('undefined')), list.join('\n'));
});

test('browser candidates on Windows skip folders the environment does not name', () => {
  const list = chrome.browserCandidates('win32', { PROGRAMFILES: 'C:\\Program Files' });
  assert.ok(list.length > 0);
  assert.ok(list.every((p) => p.startsWith('C:\\Program Files\\')), list.join('\n'));
});

test('browser candidates on Linux cover the stable packages', () => {
  const list = chrome.browserCandidates('linux', {});
  const chromeAt = list.indexOf('/usr/bin/google-chrome-stable');
  const edgeAt = list.indexOf('/usr/bin/microsoft-edge-stable');
  const chromiumAt = list.indexOf('/usr/bin/chromium');
  assert.ok(chromeAt >= 0 && edgeAt >= 0 && chromiumAt >= 0, list.join('\n'));
  assert.ok(chromeAt < edgeAt && edgeAt < chromiumAt, list.join('\n'));
});

test('findBrowser returns the first installed browser', () => {
  const edge = '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge';
  const chromium = '/Applications/Chromium.app/Contents/MacOS/Chromium';
  assert.strictEqual(chrome.findBrowser({ platform: 'darwin', env: {}, exists: existsIn([chromium, edge]) }), edge);
  assert.strictEqual(chrome.findBrowser({ platform: 'darwin', env: {}, exists: existsIn([]) }), null);
});

test('findBrowser takes CHROME_PATH before any installed browser', () => {
  const edge = '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge';
  const found = chrome.findBrowser({ platform: 'darwin', env: { CHROME_PATH: '/opt/ci/chrome' }, exists: existsIn(['/opt/ci/chrome', edge]) });
  assert.strictEqual(found, '/opt/ci/chrome');
});

test('findBrowser does not fall back when CHROME_PATH points at nothing', () => {
  const edge = '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge';
  assert.throws(
    () => chrome.findBrowser({ platform: 'darwin', env: { CHROME_PATH: '/nope/chrome' }, exists: existsIn([edge]) }),
    (e) => e instanceof chrome.UsageError && /CHROME_PATH/.test(e.message) && /\/nope\/chrome/.test(e.message),
  );
});

test('launch options isolate the browser and fake its media', () => {
  const o = chrome.launchOptions({ executablePath: '/b', userDataDir: '/p', headed: false });
  assert.strictEqual(o.executablePath, '/b');
  assert.strictEqual(o.userDataDir, '/p');
  assert.strictEqual(o.pipe, true);
  assert.strictEqual(o.headless, true);
  assert.ok(o.timeout > 0);
  for (const flag of ['--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream', '--autoplay-policy=no-user-gesture-required', '--no-first-run']) {
    assert.ok(o.args.includes(flag), flag);
  }
  // Tabs in the background keep playing and sampling at full rate.
  for (const flag of ['--disable-background-media-suspend', '--disable-renderer-backgrounding', '--disable-background-timer-throttling', '--disable-backgrounding-occluded-windows']) {
    assert.ok(o.args.includes(flag), flag);
  }
  assert.ok(o.args.every((a) => !a.startsWith('--remote-debugging-port')), o.args.join(' '));
  assert.ok(o.args.every((a) => !a.startsWith('--allow-loopback-in-peer-connection')), o.args.join(' '));
  assert.strictEqual(chrome.launchOptions({ executablePath: '/b', userDataDir: '/p', headed: true }).headless, false);
});

test('launch options add loopback candidates only when asked', () => {
  const o = chrome.launchOptions({ executablePath: '/b', userDataDir: '/p', headed: false, allowLoopback: true });
  assert.ok(o.args.includes('--allow-loopback-in-peer-connection'), o.args.join(' '));
  assert.ok(o.args.includes('--use-fake-device-for-media-stream'), o.args.join(' '));
});

function fakeBrowser(opts = {}) {
  const state = { closed: false, killed: null, pages: 0 };
  const proc = { pid: 4242, kill: (sig) => { state.killed = sig; } };
  const browser = {
    state,
    version: async () => 'Chrome/156.0.0.0',
    process: () => proc,
    newPage: async () => {
      state.pages += 1;
      return { evaluate: async () => (opts.codecs || ['video/H264', 'video/VP9', 'video/AV1']) };
    },
    close: () => (opts.hangOnClose ? new Promise(() => {}) : Promise.resolve().then(() => { state.closed = true; })),
  };
  return browser;
}

function fakePuppeteer(browser, seen) {
  return {
    launch: async (o) => {
      seen.options = o;
      seen.profileExisted = fs.existsSync(o.userDataDir);
      if (browser instanceof Error) throw browser;
      return browser;
    },
  };
}

test('main find-browser prints the path and exits 0', async () => {
  const c = capture();
  const code = await chrome.main(['find-browser'], {
    platform: 'linux', env: {}, exists: existsIn(['/usr/bin/chromium']), out: c.out, err: c.err,
  });
  assert.strictEqual(code, 0);
  assert.deepStrictEqual(c.lines.out, ['/usr/bin/chromium']);
});

test('main find-browser exits 2 with a SKIP line when no browser is installed', async () => {
  const c = capture();
  const code = await chrome.main(['find-browser'], { platform: 'linux', env: {}, exists: existsIn([]), out: c.out, err: c.err });
  assert.strictEqual(code, 2);
  assert.match(c.lines.err.join('\n'), /^SKIP .*CHROME_PATH/m);
});

test('main exits 3 on bad arguments and on a bad CHROME_PATH', async () => {
  const c = capture();
  assert.strictEqual(await chrome.main(['nope'], { out: c.out, err: c.err }), 3);
  assert.match(c.lines.err.join('\n'), /usage/i);
  const d = capture();
  assert.strictEqual(await chrome.main(['find-browser'], { platform: 'linux', env: { CHROME_PATH: '/x' }, exists: existsIn([]), out: d.out, err: d.err }), 3);
});

test('launch --probe reports the browser, closes it and removes its temporary profile', async () => {
  const c = capture();
  const browser = fakeBrowser();
  const seen = {};
  const code = await chrome.main(['launch', '--probe'], {
    platform: 'linux', env: {}, exists: existsIn(['/usr/bin/chromium']), out: c.out, err: c.err,
    puppeteer: fakePuppeteer(browser, seen),
  });
  assert.strictEqual(code, 0, c.lines.err.join('\n'));
  assert.strictEqual(seen.options.executablePath, '/usr/bin/chromium');
  assert.strictEqual(seen.options.pipe, true);
  assert.strictEqual(seen.profileExisted, true);
  assert.ok(seen.options.userDataDir.startsWith(os.tmpdir()), seen.options.userDataDir);
  assert.strictEqual(fs.existsSync(seen.options.userDataDir), false);
  assert.strictEqual(browser.state.closed, true);
  assert.strictEqual(browser.state.killed, null);
  const text = c.lines.out.join('\n');
  assert.match(text, /^PASS launch .*Chrome\/156\.0\.0\.0.*pid 4242/m);
  assert.match(text, /video\/H264/);
});

test('launch --probe keeps the profile under --log-dir', async () => {
  const logDir = fs.mkdtempSync(path.join(os.tmpdir(), 'chrome-control-test-'));
  try {
    const c = capture();
    const seen = {};
    const code = await chrome.main(['launch', '--probe', '--log-dir', logDir], {
      platform: 'linux', env: {}, exists: existsIn(['/usr/bin/chromium']), out: c.out, err: c.err,
      puppeteer: fakePuppeteer(fakeBrowser(), seen),
    });
    assert.strictEqual(code, 0, c.lines.err.join('\n'));
    assert.strictEqual(path.dirname(seen.options.userDataDir), logDir);
    assert.strictEqual(fs.existsSync(seen.options.userDataDir), true);
  } finally {
    fs.rmSync(logDir, { recursive: true, force: true });
  }
});

test('launch --probe kills only its own browser when close hangs', async () => {
  const c = capture();
  const browser = fakeBrowser({ hangOnClose: true });
  const code = await chrome.main(['launch', '--probe'], {
    platform: 'linux', env: {}, exists: existsIn(['/usr/bin/chromium']), out: c.out, err: c.err,
    puppeteer: fakePuppeteer(browser, {}), closeTimeoutMs: 50,
  });
  assert.strictEqual(code, 0, c.lines.err.join('\n'));
  assert.strictEqual(browser.state.killed, 'SIGKILL');
});

test('launch --probe exits 1 with a FAIL line when the browser does not start', async () => {
  const c = capture();
  const seen = {};
  const code = await chrome.main(['launch', '--probe'], {
    platform: 'linux', env: {}, exists: existsIn(['/usr/bin/chromium']), out: c.out, err: c.err,
    puppeteer: fakePuppeteer(new Error('Failed to launch the browser process'), seen),
  });
  assert.strictEqual(code, 1);
  assert.match(c.lines.out.concat(c.lines.err).join('\n'), /^FAIL launch .*Failed to launch/m);
  assert.strictEqual(fs.existsSync(seen.options.userDataDir), false);
});

test('launch --probe exits 2 with a SKIP line when no browser is installed', async () => {
  const c = capture();
  const code = await chrome.main(['launch', '--probe'], {
    platform: 'linux', env: {}, exists: existsIn([]), out: c.out, err: c.err,
    puppeteer: fakePuppeteer(fakeBrowser(), {}),
  });
  assert.strictEqual(code, 2);
  assert.match(c.lines.err.join('\n'), /^SKIP /m);
});

test('the command line exits 3 with usage on no arguments', () => {
  const r = spawnSync(process.execPath, [SCRIPT], { encoding: 'utf8' });
  assert.strictEqual(r.status, 3, r.stderr);
  assert.match(r.stderr, /usage/i);
});

test('the command line finds the browser CHROME_PATH names', () => {
  const r = spawnSync(process.execPath, [SCRIPT, 'find-browser'], {
    encoding: 'utf8', env: { ...process.env, CHROME_PATH: process.execPath },
  });
  assert.strictEqual(r.status, 0, r.stderr);
  assert.strictEqual(r.stdout.trim(), process.execPath);
});

test('the command line probes the installed browser', (t) => {
  const browser = chrome.findBrowser({ platform: process.platform, env: process.env, exists: chrome.isExecutable });
  if (!browser) {
    t.skip('no Chrome, Edge or Chromium installed');
    return;
  }
  const r = spawnSync(process.execPath, [SCRIPT, 'launch', '--probe'], { encoding: 'utf8', timeout: 60000 });
  assert.strictEqual(r.status, 0, r.stdout + r.stderr);
  assert.match(r.stdout, /^PASS launch /m);
  assert.match(r.stdout, /video\/H264/);
});

test('parseArgs reads serve with its port and host', () => {
  assert.deepStrictEqual(chrome.parseArgs(['serve', '--port', '15609']), { command: 'serve', port: 15609, host: '127.0.0.1' });
  assert.deepStrictEqual(chrome.parseArgs(['serve', '--port', '0', '--host', '0.0.0.0']), { command: 'serve', port: 0, host: '0.0.0.0' });
  assert.throws(() => chrome.parseArgs(['serve']), /--port/);
  assert.throws(() => chrome.parseArgs(['serve', '--port']), /--port/);
  assert.throws(() => chrome.parseArgs(['serve', '--port', 'x']), /--port.*x/);
  assert.throws(() => chrome.parseArgs(['serve', '--port', '70000']), /--port.*70000/);
  assert.throws(() => chrome.parseArgs(['serve', '--port', '1', '--host']), /--host/);
  assert.throws(() => chrome.parseArgs(['serve', '--port', '1', '--bogus']), /--bogus/);
});

test('the default serve folders are the page and the vendored players', () => {
  const dirs = chrome.defaultServeDirs();
  assert.ok(fs.existsSync(path.join(dirs.pageDir, 'index.html')), dirs.pageDir);
  assert.ok(fs.existsSync(path.join(dirs.playersDir, 'mpegts-1.7.3.min.js')), dirs.playersDir);
  assert.ok(fs.existsSync(path.join(dirs.playersDir, 'hls-1.4.14.min.js')), dirs.playersDir);
});

async function withPageServer(fn) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chrome-control-serve-'));
  const pageDir = path.join(root, 'page');
  const playersDir = path.join(root, 'players');
  fs.mkdirSync(pageDir);
  fs.mkdirSync(playersDir);
  fs.writeFileSync(path.join(pageDir, 'index.html'), '<p>index</p>');
  fs.writeFileSync(path.join(pageDir, 'srs-test.js'), '// page');
  fs.writeFileSync(path.join(playersDir, 'p.min.js'), '// player');
  fs.writeFileSync(path.join(root, 'secret.txt'), 'secret');
  const server = chrome.createPageServer({ pageDir, playersDir });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  try {
    await fn(base);
  } finally {
    await new Promise((resolve) => server.close(resolve));
    fs.rmSync(root, { recursive: true, force: true });
  }
}

// fetch normalizes "..", so send the raw path over a socket.
function rawGet(base, rawPath) {
  const u = new URL(base);
  return new Promise((resolve, reject) => {
    const req = require('node:http').request({ host: u.hostname, port: u.port, path: rawPath, method: 'GET' }, (res) => {
      let body = '';
      res.on('data', (d) => { body += d; });
      res.on('end', () => resolve({ status: res.statusCode, body }));
    });
    req.on('error', reject);
    req.end();
  });
}

test('the page server serves the page at / and the players under /players/', async () => {
  await withPageServer(async (base) => {
    let r = await fetch(`${base}/?mode=whep&url=x`);
    assert.strictEqual(r.status, 200);
    assert.match(r.headers.get('content-type'), /^text\/html/);
    assert.strictEqual(r.headers.get('cache-control'), 'no-store');
    assert.strictEqual(await r.text(), '<p>index</p>');
    r = await fetch(`${base}/srs-test.js`);
    assert.strictEqual(r.status, 200);
    assert.match(r.headers.get('content-type'), /^text\/javascript/);
    r = await fetch(`${base}/players/p.min.js`);
    assert.strictEqual(r.status, 200);
    assert.strictEqual(await r.text(), '// player');
    r = await fetch(`${base}/players/p.min.js`, { method: 'HEAD' });
    assert.strictEqual(r.status, 200);
  });
});

test('the page server answers 404, 405 and never leaves its folders', async () => {
  await withPageServer(async (base) => {
    assert.strictEqual((await fetch(`${base}/nope.js`)).status, 404);
    assert.strictEqual((await fetch(`${base}/players/`)).status, 404);
    assert.strictEqual((await fetch(`${base}/`, { method: 'POST', body: 'x' })).status, 405);
    for (const p of ['/../secret.txt', '/players/../../secret.txt', '/%2e%2e/secret.txt', '/players/%2e%2e%2f..%2fsecret.txt', '/%00']) {
      const r = await rawGet(base, p);
      assert.ok(r.status === 404 || r.status === 400, `${p} ${r.status}`);
      assert.doesNotMatch(r.body, /secret/, p);
    }
  });
});

test('the command line serves until it is stopped', async () => {
  const { spawn } = require('node:child_process');
  const child = spawn(process.execPath, [SCRIPT, 'serve', '--port', '0'], { stdio: ['ignore', 'pipe', 'pipe'] });
  let out = '';
  const port = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`no serving line: ${out}`)), 10000);
    child.stdout.on('data', (d) => {
      out += d;
      const m = out.match(/^serving http:\/\/127\.0\.0\.1:(\d+)\//m);
      if (m) { clearTimeout(timer); resolve(Number(m[1])); }
    });
  });
  const r = await fetch(`http://127.0.0.1:${port}/`);
  assert.strictEqual(r.status, 200);
  assert.match(await r.text(), /srs-test\.js/);
  const code = await new Promise((resolve) => { child.on('exit', (c) => resolve(c)); child.kill('SIGTERM'); });
  assert.strictEqual(code, 0);
});

test('the command line fails when the port is taken', async () => {
  const blocker = require('node:http').createServer();
  await new Promise((resolve) => blocker.listen(0, '127.0.0.1', resolve));
  try {
    const r = spawnSync(process.execPath, [SCRIPT, 'serve', '--port', String(blocker.address().port)], { encoding: 'utf8', timeout: 10000 });
    assert.strictEqual(r.status, 1, r.stdout + r.stderr);
    assert.match(r.stdout + r.stderr, /^FAIL serve .*EADDRINUSE/m);
  } finally {
    blocker.close();
  }
});

// On Windows the browser's helper processes can hold the profile for seconds
// after close under load; a profile left behind warns but does not fail.
test('removeProfile warns when the profile cannot be removed', () => {
  const { lines, err } = capture();
  const calls = [];
  const rm = (dir, opts) => { calls.push([dir, opts]); const e = new Error('EPERM, Permission denied'); e.code = 'EPERM'; throw e; };
  assert.strictEqual(chrome.removeProfile({ dir: '/tmp/chrome-control-x', keep: false }, err, rm), false);
  assert.strictEqual(calls.length, 1);
  assert.ok(calls[0][1].maxRetries * calls[0][1].retryDelay >= 3000);
  assert.deepStrictEqual(lines.err, ['profile /tmp/chrome-control-x not removed: EPERM, Permission denied']);
});

test('removeProfile removes a temporary profile and keeps a log-folder one', () => {
  const { lines, err } = capture();
  const calls = [];
  const rm = (dir) => { calls.push(dir); };
  assert.strictEqual(chrome.removeProfile({ dir: '/tmp/a', keep: false }, err, rm), true);
  assert.strictEqual(chrome.removeProfile({ dir: '/logs/b', keep: true }, err, rm), true);
  assert.deepStrictEqual(calls, ['/tmp/a']);
  assert.deepStrictEqual(lines.err, []);
});
