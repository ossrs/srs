---
name: srs-develop
description: Develop, modify, debug, and maintain the next-generation SRS media server written in Go — including the proxy, origin, and edge servers. This is the AI-maintained successor to the first-generation C++ SRS server. Use for all development tasks, for example, adding features, fixing bugs, refactoring code, understanding code architecture, reviewing changes, and writing tests for the Go codebase. NOT for end-user support, usage questions, configuration help, or learning how to use SRS — use the srs-support skill for those. Only activate when the task is explicitly about developing or modifying the Go SRS codebase.
---

# SRS Development

## Core Principle

**Code is the only truth.** Documents may be outdated. Issue descriptions may be inaccurate. Pull requests may be misleading. Feature descriptions may be insufficient. Always ground your understanding in the actual source code. You may read docs and issues for context, but verify everything against the code.

---

## Task Router (MANDATORY — Step 1)

Route the user's request to exactly ONE task type. Follow that task only. Do not combine tasks.

| Task | Route To | Status |
|---|---|---|
| **Develop a Feature** | → [Develop a Feature](#task-develop-a-feature) | ✅ Supported |
| **Fix a Bug** | → [Fix a Bug](#task-fix-a-bug) | ❌ Not yet supported |
| **Learn Code** | → [Learn Code](#task-learn-code) | ❌ Not yet supported |
| **Review a PR** | → [Review a PR](#task-review-a-pr) | ❌ Not yet supported |

**If the routed task is not yet supported**, stop and tell the user:
- What task type you routed to
- That this task type is not supported yet
- That support will be added in the future

Do NOT attempt unsupported tasks.

---

## Task: Fix a Bug

**Not yet supported.** Will be added in a future update.

---

## Task: Learn Code

**Not yet supported.** Will be added in a future update.

---

## Task: Review a PR

**Not yet supported.** Will be added in a future update.

---

## Task: Develop a Feature

**Important:** The C++ media server (origin + edge) is in **maintenance mode** — only bug fixes are accepted, no new features. All new feature development happens in the **next-generation Go server**. You may reference the C++ server's code to understand how things were done before, but do not add features to it.

**Service Router (Step 2)** — Determine which Go service the feature targets. Route to exactly ONE service. Do not guess — if unclear, ask the user to clarify.

| Service | Route To | Status |
|---|---|---|
| **Proxy server** | → [Proxy Server](#proxy-server) | ✅ Supported |
| **Origin server** | → [Origin Server](#origin-server) | ✅ Supported |
| **Edge server** | → [Edge Server](#edge-server) | ❌ Not yet supported |

**If the routed service is not yet supported**, stop and tell the user:
- What service you routed to
- That this service is not supported yet

### Proxy Server

*(workflow steps to be defined)*

### Origin Server

*(workflow steps to be defined)*

### Edge Server

**Not yet supported.** Will be added in a future update.
