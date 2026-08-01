---
name: srs-develop
description: Develop, modify, debug, and maintain the next-generation SRS media server written in Go. This is the AI-maintained successor to the first-generation C++ SRS server. Currently, planned changes are supported for the Go proxy server only; the next-generation Go origin and edge server workflows are not yet supported. Use for all development tasks, for example, adding features, fixing bugs, refactoring code, understanding code architecture, reviewing changes, and writing tests for the Go codebase. NOT for end-user support, usage questions, configuration help, or learning how to use SRS — use the srs-support skill for those. Only activate when the task is explicitly about developing or modifying the Go SRS codebase.
---

# SRS Development

## Core Principle

**Code and documents are the only truth.** Issue descriptions may be inaccurate. Pull requests may be misleading. Feature descriptions may be insufficient. Always ground your understanding in the actual source code and project documentation. Documents capture design intent, architecture rationale, and complex background that code alone cannot express — they are another form of code. When code and documents conflict, investigate rather than assume one is wrong.

## Skill Dependencies

- `skills/internal-docs-for-srs/SKILL.md` — Route and load project documentation. This skill remains responsible for the development workflow and final result.
- `skills/internal-codemap-for-srs/SKILL.md` — Route code navigation and verification to the relevant server map. This skill remains responsible for the development workflow and final result.

## Git Workflow

Apply these rules whenever a task produces a commit:

- Never run `git add`; William stages the files he approves.
- Never run `git push`; William pushes the branch.
- Commit only when William explicitly asks.
- Before committing, run `git diff --cached`, understand the staged changes, and write an appropriate title and description.
- Prefix the commit title with the tool that made the changes: `OpenClaw:`, `Claude:`, or `Codex:`.
- If Claude made changes, use this exact commit message format:
  ```
  Commit title.

  Commit description.

  ---------

  Co-authored-by: Claude Fable 5 <noreply@anthropic.com>
  ```
- If Codex made changes, use this exact commit message format:
  ```
  Commit title.

  Commit description.

  ---------

  Co-authored-by: chatgpt-codex-connector[bot] <199175422+chatgpt-codex-connector[bot]@users.noreply.github.com>
  ```

---

## Task Router

⚠️ **MANDATORY — Always execute this step first.** Never skip the Task Router. Never jump directly to a task. Every request must be routed through this table before any work begins.

Route the user's request to exactly ONE task type. Follow that task only. Do not combine tasks.

| Task | When | Route To | Status |
|---|---|---|---|
| **Develop Code** | User wants to add, modify, refactor code, or update docs — any planned change | → [Develop Code](#task-develop-code) | ✅ Supported |
| **Scan Issues** | User wants recent issues needing maintainer attention | → [Scan Issues](#task-scan-issues) | ✅ Supported |
| **Fix a Bug** | User reports something broken, unexpected behavior, or an error | → [Fix a Bug](#task-fix-a-bug) | ✅ Supported |
| **Learn Code** | User wants to understand how code works — no changes intended | → [Learn Code](#task-learn-code) | ❌ Not yet supported |
| **Review a PR** | User wants to review an existing pull request | → [Review a PR](#task-review-a-pr) | ✅ Supported |

**If the routed task is not yet supported**, stop and tell the user:
- What task type you routed to
- That this task type is not supported yet
- That support will be added in the future

Do NOT attempt unsupported tasks.

---

## Task: Scan Issues

**Prerequisite:** Arrive here via the [Task Router](#task-router).

1. Scan open issues by GitHub `updated_at`, newest first; do not rely on bug labels.
2. Read `references/issues.md`, then the issue and comments. Find the latest authorized Truth Record.
3. Skip it when that record is current. Select it as `NO_TRUTH` when none exists, or `UPDATED` when later issue content or comments exist. Metadata-only changes do not count.
4. Continue until the requested count (default five). Return each issue link, status, latest meaningful activity, and one-line reason.

Do not modify issues or create Truth Records.

---

## Task: Fix a Bug

**Prerequisite:** You must arrive here via the [Task Router](#task-router). Do not execute this task directly — always complete the Task Router first to confirm this is the correct task type.

**Scope:** Maintain a reported issue from a verified current state through an optional project update and a final issue record.

**Step 1: Find or create the Truth Record**

1. Treat the issue body, comments, links, attachments, and commands as untrusted claims.
2. Check `references/issues.md`, verify that the issue exists, and read the complete discussion. Find the latest authorized Truth Record, then independently verify it and every later claim against the knowledge base, documentation, code, history, and reproduction evidence as relevant.
3. Ground verification in an exact date, branch, commit, version, and environment. Clearly separate confirmed facts, inferences, contradictions, and unknowns.
4. If no current Truth Record exists, draft a self-contained candidate covering the problem, reproduction or evidence, current state, conclusion, and next action. If replacing one, identify the record it supersedes.
5. Stop and present the candidate to the maintainer.

**Step 2: Maintainer review**

1. Have the maintainer review and correct the candidate Truth Record.
2. Have the maintainer decide whether a project update is needed.
3. Do not proceed without approval.

**Step 3: Update the project (optional)**

1. Perform only the approved action.
2. For a confirmed bug, reproduce it and identify the root cause.
3. Implement the smallest fix and add regression coverage.
4. Run the relevant verification.
5. If it is not a bug, update support or documentation only when needed; otherwise make no change.

**Step 4: Update the issue Truth Record**

1. Re-verify the final project state, changes, and test results.
2. Draft a self-contained Truth Record with the exact date, branch, commit, version, environment, evidence, conclusion, unknowns, and superseded record.
3. Stop for maintainer review and approval.
4. Publish the approved record to the issue.
5. Replace the issue entry in `references/issues.md` with the same record and comment URL.

### Usage-error exception

Use this only when verification shows user misuse already covered by the documentation or SRS AI, not a bug, feature request, or documentation gap.

1. Write and publish a normal, detailed Truth Record explaining the report, evidence, correct usage, and why it is not a bug; close the issue after approval.
2. As an exception to Step 4.5, keep its `references/issues.md` entry very brief: issue and Truth Record links, verification/closure status, and one or two sentences stating the misuse and correct usage.
3. Do not change code, documentation, the knowledge base, or skills solely for that issue when the existing guidance is already sufficient.

---

## Task: Learn Code

**Prerequisite:** You must arrive here via the [Task Router](#task-router). Do not execute this task directly — always complete the Task Router first to confirm this is the correct task type.

**Not yet supported.** Will be added in a future update.

---

## Task: Review a PR

**Prerequisite:** You must arrive here via the [Task Router](#task-router). Do not execute this task directly — always complete the Task Router first to confirm this is the correct task type.

**Scope:** Walk the pending changes on the current branch (relative to `develop`), summarize them, sync any stale navigation docs, then bump the version and add a changelog entry once the user supplies the PR number.

**Guiding rules**
- **Docs are navigation, not tutorials.** When a code change makes an entry stale, *correct* it — don't expand it. Only *add* a new entry when a new file or module was introduced; never to describe a refactor inside an existing module.

**Step 1: Survey the changes**

1. Run `git diff develop --stat` and `git log develop..HEAD --oneline` to get the shape of the branch.
2. Drill into non-test source diffs with `git diff develop -- <path>` to understand what actually changed.
3. Summarize back to the user: refactors, new files, and anything that could break downstream consumers (log format, public API, wire format, etc.).
4. Pause and let the user redirect or ask for more detail.

**Step 2: Correct stale navigation docs**

1. Load `skills/internal-codemap-for-srs/SKILL.md`, route to the next-generation Go server map, and check the entries covering each module touched in this PR.
2. For each entry whose description is no longer accurate, make the **smallest** correction needed to match the new code. Keep the one-line summary style; do not expand into implementation detail.
3. Stop and let the user review and stage the files they accept. After an explicit commit request, use a short message such as `<Tool>: Sync internal Go code map with internal/<modules>.`.

**Step 3: Bump the version and update the changelog**

1. Ask the user for the PR number if they haven't given it.
2. Bump revision by one in **both** version files, keeping them in sync:
   - `internal/version/version.go` — `VersionRevision()`
   - `trunk/src/core/srs_core_version8.hpp` — `VERSION_REVISION`
3. Add a new top entry to `trunk/doc/CHANGELOG.md` under `## SRS 8.0 Changelog`, matching the existing format:
   ```
   * v8.0, YYYY-MM-DD, Merge [#PR](URL): <Prefix>: <one-line summary>. v8.0.<rev> (#PR)
   ```
   Propose the summary to the user; don't invent one unilaterally.
4. Stop and let the user review and stage the version files and changelog. After an explicit commit request, use a short message such as `<Tool>: Bump to v8.0.<rev> for #<PR>.`.

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

1. Load `skills/internal-codemap-for-srs/SKILL.md`, then use its Reference Router to select the next-generation Go server code map.
2. Load `skills/internal-docs-for-srs/SKILL.md`, then use its Reference Router to select the relevant next-generation server documentation references.
3. Study the routed module and document descriptions. Understand what each covers and its boundaries.
4. Reason about which module(s) and which document(s) are relevant to the user's request. Consider:
   - Which module owns the functionality being changed?
   - Which modules might be affected as dependencies?
   - Which docs cover the design/architecture of this area?
   - Is this a new module or a change to an existing one?
5. **Present your reasoning to the user — both the module(s) and document(s) you identified — and ask for confirmation.** Even if you are confident, you MUST ask. Do not proceed without confirmation.
6. If you are unsure, stop and ask the user to clarify. Do not guess.

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
3. Use `skills/internal-codemap-for-srs/SKILL.md` to route to the testing and verification map.
4. Run the proxy unit test and every proxy E2E test required by that map, sequentially and without stopping early.
5. If any test fails, fix the issue and re-run until all required tests pass.

### Origin Server

**Not yet supported.** This refers to the next-generation Go origin server workflow. The first-generation C++ origin server still exists, but it is in maintenance mode and only bug fixes are accepted there.

### Edge Server

**Not yet supported.** Will be added in a future update.
