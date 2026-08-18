// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const projectRoot = path.resolve(__dirname, '../../..');
const playerJs = path.join(projectRoot, 'trunk/research/players/js');
const utilitySource = fs.readFileSync(path.join(playerJs, 'winlin.utility.js'), 'utf8');
const pageSource = fs.readFileSync(path.join(playerJs, 'srs.page.js'), 'utf8');

function buildDefaultFlvUrl(rawUrl) {
    const pageUrl = new URL(rawUrl);
    const context = {
        window: {
            location: {
                host: pageUrl.host,
                hostname: pageUrl.hostname,
                port: pageUrl.port,
                pathname: pageUrl.pathname,
                search: pageUrl.search,
                hash: pageUrl.hash,
                protocol: pageUrl.protocol,
            },
        },
        console,
    };

    vm.createContext(context);
    vm.runInContext(utilitySource, context);
    vm.runInContext(pageSource, context);

    return context.build_default_flv_url();
}

const cases = [
    {
        name: 'keeps the direct SRS HTTP server port',
        page: 'http://live.example.com:8080/players/srs_player.html',
        expected: 'http://live.example.com:8080/live/livestream.flv',
    },
    {
        name: 'uses the public HTTP origin',
        page: 'http://live.example.com/players/srs_player.html?stream=tv.flv',
        expected: 'http://live.example.com/live/tv.flv',
    },
    {
        name: 'uses the public HTTPS origin',
        page: 'https://live.example.com/players/srs_player.html?stream=tv.flv',
        expected: 'https://live.example.com/live/tv.flv',
    },
    {
        name: 'uses a custom reverse-proxy port',
        page: 'https://live.example.com:8443/players/srs_player.html?stream=tv.flv',
        expected: 'https://live.example.com:8443/live/tv.flv',
    },
    {
        name: 'uses the default port when schema overrides the page protocol',
        page: 'http://live.example.com:8080/players/srs_player.html?schema=https&stream=tv.flv',
        expected: 'https://live.example.com/live/tv.flv',
    },
    {
        name: 'preserves explicit target overrides',
        page: 'http://live.example.com/players/srs_player.html?schema=https&server=media.example.com&port=9443&vhost=tenant.example.com&app=show&stream=main.flv',
        expected: 'https://media.example.com:9443/show/main.flv?vhost=tenant.example.com',
    },
];

for (const testCase of cases) {
    assert.equal(buildDefaultFlvUrl(testCase.page), testCase.expected, testCase.name);
    console.log(`PASS: ${testCase.name}`);
}
