---
name: internal-audit-for-srs
description: Audit one SRS commit or review group before the user reads it, in a fresh context that sees the code before the commit message and the tests, and report what does not hold up, with a concrete scenario each. Use from the srs-autopilot review workflow, from the srs-develop Review a PR and Scan PRs workflows, or by hand on a commit, as the last check before a merge. It finds and reports; it never fixes, commits, or touches a branch. The parent skill owns the review workflow and the result.
---

# SRS Internal Change Audit

An audit is a second reading of a change by an agent that did not write it and does not share its context. It reads the code first, commits to what the code should do, and only then reads the commit message and the tests. The parent workflow (the `srs-autopilot` review, or the `srs-develop` Review a PR or Scan PRs) owns the review; this skill owns the audit's method, its report, and the list of smells.

## Why the order matters

An agent that writes a test and a fix in one run, from one bug report, gives no independent check: the test restates the fix. The commit message then explains the fix in confident words, and a reviewer who reads the message first checks the code against the message, not against the protocol. The audit breaks that chain by withholding the message and the tests until the auditor has written down its own expectations.

## Core Rules

- The auditor runs in a fresh context: a subagent that gets only the files this skill names, never the task file, the fixer's conversation, or the review session's notes.
- The commit message, the code comments and the tests are claims, not evidence. The code and the protocol are the evidence.
- Every finding needs a concrete scenario: these steps leave this state wrong, with the file and line. A worry without a scenario is listed under "Not checked", not under findings.
- "Nothing found" is a valid result, with what was tried. The auditor is not scored on the number of findings.
- The auditor never edits code, never commits, never touches any branch or worktree, and never fixes. It reads with `git show` and `git diff` only.
- The parent puts the report in front of the user before the code, and logs each finding as a work item in the task file, or in its report when it has no task file. Fixes go through the review, as the user decides.
- Match the repository's rules on the way to the result: plain commands and script files only, no multi-line shell blobs; temporary files in the scratchpad.

## Path Resolution

- Resolve `references/` and `scripts/` relative to the directory containing this `SKILL.md`.
- Resolve repository paths such as `trunk/`, `cmd/`, or `skills/` relative to the current working directory.

## Workflow: Audit a Change

The parent runs this for one commit, or for one review group (its squashed commit, or `<base>..<tip>`).

### 1. Split the change

Run `scripts/audit-diffs.sh <commit|base..tip> <out-dir>` with `<out-dir>` in the scratchpad. It writes:

- `code.diff` — the change without its tests.
- `stat.txt` — the code files changed.
- `base.txt` — the commit before the change, for reading the code around it with `git show <base>:<path>`.
- `tests.diff` — the test files changed: `trunk/src/utest`, `cmd/integ`, `trunk/3rdparty/srs-bench`, `*_test.go`, `*test*.sh`.
- `message.txt` — the commit messages.
- `tip.txt` — the audited commit.

The auditor gets the first three in phase 1 and the rest in phase 2. It returns each phase's sections as text in its report, and the parent saves them to `<dir>/audit.md`: a subagent is refused a Write of a report file, so the auditor does not write one.

### 2. Phase 1, blind

Start one subagent with this prompt, with `<dir>` filled in:

```
Audit a code change blind. Load skills/internal-audit-for-srs/SKILL.md and its references/report-template.md.
Inputs: <dir>/code.diff (the change without its tests), <dir>/stat.txt, and <dir>/base.txt (the commit before the change; read the code around the change with `git show <base>:<path>`, and the docs at `git show <base>:trunk/conf/full.conf` and the srs-develop and internal-docs-for-srs skills).
You have no commit message, no tests, and no task text on purpose: do not run git log on the branch, do not read the test directories, and do not read tasks/.
Return sections 1 to 4 of the report template as text, verbatim, in your report; do not write files: the change's files; the expectations at each step of the lifecycle the change touches, each with its source (the protocol, full.conf, the docs, or the sibling code that does the same thing for another protocol); the blind read, what the code does in plain words, without guessing the intent; and the tests you would write. Then stop.
```

Save the returned sections to `<dir>/audit.md`.

### 3. Phase 2, the claims

Continue the same subagent (SendMessage, so its expectations stay in its context) with:

```
Phase 2. The audited commit is <tip>. Read <dir>/message.txt and <dir>/tests.diff, and load references/smells.md.
Return sections 5 to 10 of the report template as text, verbatim, in your report; do not write files: the code against the message; the test gaps, from your own list against the tests present; the decisions the change makes without saying so; the smells matched; the findings, ranked, each with a concrete scenario and the file and line, or "Nothing found" with what you tried; and what the audit could not check.
Never edit code, never commit, never touch any branch. End with the one-line summary of each finding, or that nothing was found.
```

Append the returned sections to `<dir>/audit.md`.

### 4. Hand over

The parent reads the report, tells the user the findings and the test gaps before the user reads the code, and logs each finding as a work item in the task file, or in its report when it has no task file. The audit's report path goes in the Work log entry.

## Report

`references/report-template.md` is the report's shape. Keep its sections and order: the first four prove the auditor committed before it saw the claims; the user can tell a real audit from a rubber stamp by reading them.

## Smells

`references/smells.md` lists the mistakes this project has seen an agent make, one line each, with the commit that showed it. The auditor reads it in phase 2 only, so the list guides the check and does not replace the blind read. Add a line whenever the user's own read catches something the audit missed.
