---
name: srs-develop
description: Develop, modify, debug, and maintain the next-generation SRS media server written in Go. This is the AI-maintained successor to the first-generation C++ SRS server. Currently, planned changes are supported for the Go proxy server only; the next-generation Go origin and edge server workflows are not yet supported. Use for all development tasks, for example, adding features, fixing bugs, refactoring code, understanding code architecture, reviewing changes, and writing tests for the Go codebase. NOT for end-user support, usage questions, configuration help, or learning how to use SRS — use the srs-support skill for those. Only activate when the task is explicitly about developing or modifying the Go SRS codebase.
---

# SRS Development

## Core Principle

**Code and documents are the only truth.** Issue descriptions may be inaccurate. Pull requests may be misleading. Feature descriptions may be insufficient. Always ground your understanding in the actual source code and project documentation. Documents capture design intent, architecture rationale, and complex background that code alone cannot express — they are another form of code. When code and documents conflict, investigate rather than assume one is wrong.

---

## Task Router

⚠️ **MANDATORY — Always execute this step first.** Never skip the Task Router. Never jump directly to a task. Every request must be routed through this table before any work begins.

Route the user's request to exactly ONE task type. Follow that task only. Do not combine tasks.

| Task | When | Route To | Status |
|---|---|---|---|
| **Develop Code** | User wants to add, modify, refactor code, or update docs — any planned change | → [Develop Code](#task-develop-code) | ✅ Supported |
| **Fix a Bug** | User reports something broken, unexpected behavior, or an error | → [Fix a Bug](#task-fix-a-bug) | ❌ Not yet supported |
| **Learn Code** | User wants to understand how code works — no changes intended | → [Learn Code](#task-learn-code) | ❌ Not yet supported |
| **Review a PR** | User wants to review an existing pull request | → [Review a PR](#task-review-a-pr) | ✅ Supported |

**If the routed task is not yet supported**, stop and tell the user:
- What task type you routed to
- That this task type is not supported yet
- That support will be added in the future

Do NOT attempt unsupported tasks.

---

## Task: Fix a Bug

**Prerequisite:** You must arrive here via the [Task Router](#task-router). Do not execute this task directly — always complete the Task Router first to confirm this is the correct task type.

**Not yet supported.** Will be added in a future update.

---

## Task: Learn Code

**Prerequisite:** You must arrive here via the [Task Router](#task-router). Do not execute this task directly — always complete the Task Router first to confirm this is the correct task type.

**Not yet supported.** Will be added in a future update.

---

## Task: Review a PR

**Prerequisite:** You must arrive here via the [Task Router](#task-router). Do not execute this task directly — always complete the Task Router first to confirm this is the correct task type.

**Scope:** Walk the pending changes on the current branch (relative to `develop`), summarize them, sync any stale navigation docs, then bump the version and add a changelog entry once the user supplies the PR number.

**Guiding rules**
- **The user drives staging.** Never `git add` on your own. After each step, stop and wait for the user to review and stage the files they approve. Only run `git commit` when they say so.
- **Docs are navigation, not tutorials.** When a code change makes an entry stale, *correct* it — don't expand it. Only *add* a new entry when a new file or module was introduced; never to describe a refactor inside an existing module.

**Step 1: Survey the changes**

1. Run `git diff develop --stat` and `git log develop..HEAD --oneline` to get the shape of the branch.
2. Drill into non-test source diffs with `git diff develop -- <path>` to understand what actually changed.
3. Summarize back to the user: refactors, new files, and anything that could break downstream consumers (log format, public API, wire format, etc.).
4. Pause and let the user redirect or ask for more detail.

**Step 2: Correct stale navigation docs**

1. Check `.openclaw/memory/srs-codebase-map.md` for entries covering any module touched in this PR.
2. For each entry whose description is no longer accurate, make the **smallest** correction needed to match the new code. Keep the one-line summary style; do not expand into implementation detail.
3. Stop. Let the user review. When they `git add` the files they accept, commit with a short message in the existing style, e.g. `Claude: Sync srs-codebase-map with internal/<modules>.`.

**Step 3: Bump the version and update the changelog**

1. Ask the user for the PR number if they haven't given it.
2. Bump revision by one in **both** version files, keeping them in sync:
   - `internal/version/version.go` — `VersionRevision()`
   - `trunk/src/core/srs_core_version7.hpp` — `VERSION_REVISION`
3. Add a new top entry to `trunk/doc/CHANGELOG.md` under `## SRS 7.0 Changelog`, matching the existing format:
   ```
   * v7.0, YYYY-MM-DD, Merge [#PR](URL): <Prefix>: <one-line summary>. v7.0.<rev> (#PR)
   ```
   Propose the summary to the user; don't invent one unilaterally.
4. Stop. Let the user review. When they `git add` the version files and changelog, commit with a short message like `Proxy: Bump to v7.0.<rev> for #<PR>.`.

---

## Task: Develop Code

**Prerequisite:** You must arrive here via the [Task Router](#task-router). Do not execute this task directly — always complete the Task Router first to confirm this is the correct task type.

**Scope:** This task covers any planned code or documentation change — adding new features, modifying existing functionality, refactoring code, and updating documentation.

**Important:** The C++ media server (origin + edge) is in **maintenance mode** — only bug fixes are accepted, no new features. All new feature development happens in the **next-generation Go server**. You may reference the C++ server's code to understand how things were done before, but do not add features to it.

**Service Router** — Determine which Go service the feature targets. Route to exactly ONE service. Do not guess — if unclear, ask the user to clarify.

| Service | Route To | Status |
|---|---|---|
| **Proxy server** | → [Proxy Server](#proxy-server) | ✅ Supported |
| **Origin server** | → [Origin Server](#origin-server) | ❌ Not yet supported |
| **Edge server** | → [Edge Server](#edge-server) | ❌ Not yet supported |

**If the routed service is not yet supported**, stop and tell the user:
- What service you routed to
- That this service is not supported yet

### Proxy Server

The proxy server is a complex, growing product — not a small app. It has many modules, and more will be added over time. You cannot load all the code into context at once. The key to working on it is **routing to the correct module first**.

**Step 1: Module Routing (MANDATORY)**

1. Read the codebase map: `memory/srs-codebase-map.md` — both the **Next-Generation Server Code** section (code modules: `cmd/` + `internal/`) and the **Next-Generation Server Docs** section (documentation: `docs/proxy/`).
2. Study the module descriptions and doc descriptions. Understand what each covers and its boundaries.
3. Reason about which module(s) and which doc(s) are relevant to the user's request. Consider:
   - Which module owns the functionality being changed?
   - Which modules might be affected as dependencies?
   - Which docs cover the design/architecture of this area?
   - Is this a new module or a change to an existing one?
4. **Present your reasoning to the user — both the module(s) and doc(s) you identified — and ask for confirmation.** Even if you are confident, you MUST ask. Do not proceed without confirmation.
5. If you are unsure, stop and ask the user to clarify. Do not guess.

Only after the user confirms the routing do you proceed to Step 2.

**Step 2: Understand the Module**

1. **Read the confirmed docs** (if any were identified) — understand the design intent, architecture rationale, and how the module is organized internally. This is the *why*.
2. **Based on doc understanding, identify the specific file(s)** within the module that are relevant to the feature. Not the whole module — only the files that matter.
3. **Read only those specific files.** Code gives you the implementation details: function signatures, patterns, conventions, edge cases. This is the *how*.
4. If no relevant docs exist, scan the module directory listing (filenames only) to locate the right files, then read them.

**Step 3: Implement and Verify**

1. Implement the code change.
2. If you changed or added a Go interface with a `//go:generate go tool counterfeiter ...` directive, regenerate fakes:
   ```
   make generate
   ```
3. Run the proxy unit tests to verify:
   ```
   bash scripts/proxy-utest.sh --coverage
   ```
4. Run the proxy E2E tests:
   - Single-origin RTMP proxy test (starts proxy + one SRS origin, publishes RTMP, verifies playback):
   ```
   bash scripts/proxy-e2e-test.sh
   ```
   - Multi-origin cluster routing test (starts proxy + two SRS origins, publishes multiple streams, verifies streams are assigned to different origins):
   ```
   bash scripts/proxy-e2e-cluster-test.sh
   ```
   - Redis multi-proxy routing test (requires local Redis; starts two proxy instances with Redis LB, publishes through one proxy, verifies playback through the other):
   ```
   bash scripts/proxy-e2e-redis-test.sh
   ```
   - RTMP transmuxing test (starts proxy + one SRS origin, publishes RTMP, verifies RTMP/HTTP-FLV/HLS playback, and verifies WebRTC WHEP playback when `PROXY_TRANSMUX_TEST_RTC=on`):
   ```
   bash scripts/proxy-e2e-transmux-test.sh
   ```
5. If any tests fail, fix the issues and re-run until all tests pass.

All script paths are relative to this skill's directory.

### Origin Server

**Not yet supported.** This refers to the next-generation Go origin server workflow. The first-generation C++ origin server still exists, but it is in maintenance mode and only bug fixes are accepted there.

### Edge Server

**Not yet supported.** Will be added in a future update.
