---
name: internal-audit-for-srs
description: Use to check one SRS commit or review group for defects before the user reads it or before a merge, when the srs-autopilot review workflow or the srs-develop Review a PR and Scan PRs workflows ask for an audit, after a cherry-pick to another branch, or when the user asks to audit a commit. Proves one P0 or P1 bug in the change with a failing test; fixing, committing and the review result stay with the caller.
---


# SRS Internal Change Audit

An audit is a second reading of a change by agents that did not write it and do not share its context. The parent workflow (the `srs-autopilot` review, or the `srs-develop` Review a PR, Scan PRs or a cherry-pick) owns the review; this skill owns the audit's method, its report, and the list of smells.

## The goal: prove one bug

The audit starts from one premise: this change has a P0 or P1 bug. The auditors' job is not to look around and see whether a bug turns up. Their job is to prove one bug is there.

- **One bug.** The audit is done when one bug is proven. Prove a P0 first; only when no P0 can be proven, prove a P1. Do not collect a list.
- **Understand** the change: the code, the code around it, the protocol and the docs, then the message and the tests. This is how an auditor finds where the bug is, not the result.
- **Argue** each suspect from the code: the inputs and steps that reach it, and the state that is wrong after them, with the file and line.
- **Prove** it with a test that fails at the audited commit. A red test is the proof; an argument without one is a suspect, not a finding.

Work hard at it. Attack each suspect with a test before giving it up, and when one test passes, try the next path to the same place: another input, another order, a close or an error in the middle, another protocol that shares the code.

## Three auditors

One auditor follows one line of thought and misses what lies off it. The parent starts three auditors, no more, at the same time, each in its own fresh context, so each reads the change its own way.

- No one assigns the auditors a direction. Each reads the change and chooses its own approach to proving the bug, from what this change could break.
- At the end of phase 1, each auditor names its approach and its main suspects.
- When two auditors chose the same approach or the same top suspect, the parent asks one of them to take another approach, of its own choosing. The parent does not say which.

## Why the order matters

An agent that writes a test and a fix in one run, from one bug report, gives no independent check: the test restates the fix. The commit message then explains the fix in confident words, and a reviewer who reads the message first checks the code against the message, not against the protocol. The audit breaks that chain by withholding the message and the tests until each auditor has written down its own expectations and suspects.

## Core Rules

- Each auditor runs in a fresh context: a subagent that gets only the files this skill names, never the task file, the fixer's conversation, the review session's notes, or another auditor's report.
- The commit message, the code comments and the tests are claims, not evidence. The code, the protocol and a failing test are the evidence.
- Prove only P0 and P1 bugs:
  - **P0:** a crash, an abort, a read or write past a buffer, a use-after-free or double free, a security hole, or a leak on a path a client can repeat.
  - **P1:** a valid client refused or broken, a stream or its data lost, or a wrong result a user or an operator relies on.
- Leave out everything smaller: style, naming, logs, comments, a missing test alone, and a rare corner case with no harm.
- A proven bug has:
  - The scenario: these steps leave this state wrong, with the file and line.
  - The test, and its failing output at the audited commit.
- A suspect an auditor could not prove goes under "Not proven", with the test it tried and why it passed or could not run. It is never reported as a bug.
- Never invent a bug to meet the premise. "No bug proven" is a valid result, but only after the suspects were attacked with tests, and the report lists them.
- An auditor never edits the audited branch or its worktree, never commits, and never fixes. It reads with `git show` and `git diff`, and writes its proof tests only in its own proof worktree.
- The parent puts the result in front of the user before the code, and logs the proven bug as a work item in the task file, or in its report when it has no task file. Fixes go through the review, as the user decides.
- Match the repository's rules on the way to the result: plain commands and script files only, no multi-line shell blobs; temporary files in the scratchpad.

## Path Resolution

- Resolve `references/` and `scripts/` relative to the directory containing this `SKILL.md`.
- Resolve repository paths such as `trunk/`, `cmd/`, or `skills/` relative to the current working directory.

## Workflow: Audit a Change

The parent runs this for one commit, or for one review group (its squashed commit, or `<base>..<tip>`). Steps 2 to 4 run for the three auditors at the same time; `<n>` is the auditor's number, 1 to 3, and `<dir>` is the split directory of step 1.

### 1. Split the change

Run `scripts/audit-diffs.sh <commit|base..tip> <out-dir>` with `<out-dir>` in the scratchpad. It writes:

- `code.diff` — the change without its tests.
- `stat.txt` — the code files changed.
- `base.txt` — the commit before the change, for reading the code around it with `git show <base>:<path>`.
- `tests.diff` — the test files changed: `trunk/src/utest`, `cmd/integ`, `trunk/3rdparty/srs-bench`, `*_test.go`, `*test*.sh`.
- `message.txt` — the commit messages.
- `tip.txt` — the audited commit.

The auditors get the first three in phase 1 and the rest in phase 2. Each returns its sections as text in its report, and the parent saves them to `<dir>/audit-<n>.md`: a subagent is refused a Write of a report file, so an auditor does not write one.

### 2. Phase 1, blind

Start the three subagents in one message, each with this prompt, with `<dir>` and `<n>` filled in:

```
Audit a code change blind. Load skills/internal-audit-for-srs/SKILL.md and its references/report-template.md.
The premise: this change has a P0 or P1 bug, and your job is to prove one, not to see whether one turns up. You are auditor <n> of 3; the others work alone. Choose your own approach to proving the bug, from what this change could break.
Inputs: <dir>/code.diff (the change without its tests), <dir>/stat.txt, and <dir>/base.txt (the commit before the change; read the code around the change with `git show <base>:<path>`, and the docs at `git show <base>:trunk/conf/full.conf` and the srs-develop and internal-docs-for-srs skills).
You have no commit message, no tests, and no task text on purpose: do not run git log on the branch, do not read the test directories, do not read tasks/, and do not read the audit-*.md files.
When the change touches trunk/src, also load skills/srs-develop/references/cpp-memory-safety.md and check every changed function and class against it in section 3.
Return sections 1 to 4 of the report template as text, verbatim, in your report; do not write files: the change's files; the expectations at each step of the lifecycle the change touches, each with its source (the protocol, full.conf, the docs, or the sibling code that does the same thing for another protocol); the blind read, what the code does in plain words, without guessing the intent; and your approach in one line, then your suspects, P0 first, each with the scenario that would break it. Then stop.
```

Save each one's sections to `<dir>/audit-<n>.md`. Compare the three approaches and top suspects; when two match, ask one of those auditors, with SendMessage, to choose another approach of its own and return a new section 4. Do not suggest which.

### 3. Phase 2, the claims

Continue each subagent (SendMessage, so its expectations stay in its context) with:

```
Phase 2. The audited commit is <tip>. Read <dir>/message.txt and <dir>/tests.diff, and load references/smells.md as hints from past reviews, not a checklist.
Return sections 5 to 8 of the report template as text, verbatim, in your report; do not write files: the code against the message; the test gaps, from your suspects against the tests present; the decisions the change makes without saying so; and the smells matched. Then update your suspects with what the claims and the gaps showed, rank them, P0 first, and list them. Then stop.
Never edit code, never commit, never touch any branch.
```

Append each one's sections to its `audit-<n>.md`.

### 4. Phase 3, prove

Phase 3 builds and runs the code at the audited commit. When that commit is a pull request's head, from Review a PR or Scan PRs, the parent first reads every changed file that runs during configure, build or test:

- `trunk/configure`, `trunk/auto/`, Makefiles, and shell, Python or Go scripts.
- Test code: `trunk/src/utest`, `cmd/integ`, `*_test.go`.
- `trunk/3rdparty/` and `.github/`.

The parent judges whether running them could harm this machine, its data or its accounts, and skips phase 3 when any of them could, or when it cannot tell what a change does.

A skipped phase 3 is a finding: the parent tells the user the risky lines, with the file and line, and the audit ends after phase 2 with nothing run. When the check finds nothing, phase 3 runs.

Create a proof worktree per auditor, detached at the audited commit, so no branch moves: `git worktree add --detach <dir>/proof-<n> <tip>`. These worktrees are the audit's own, an exception to the worktree rule in `skills/srs-develop/SKILL.md`. Note `git worktree list` before creating them. Continue each subagent with:

```
Phase 3. Prove one bug. Your proof worktree is <dir>/proof-<n>, detached at <tip>; write and run tests only there.
Take your suspects in order, P0 first: write the smallest test that reproduces the scenario, build, run it, and keep its output. Stop at the first red test of a P0. Only when no P0 suspect is left, go on to the P1 suspects, and stop at the first red test.
Pick the test surface from the Tests and Verification section of skills/internal-codemap-for-srs/references/cpp-server.md for C++, and run the sanitizer checks of skills/srs-develop/references/cpp-memory-safety.md for a memory bug; use go test for Go code and the script itself for a script. A new C++ test goes in a new srs_utest_aiNN file.
A red test proves the bug. A green test does not end the suspect: try the other paths to the same code before you give it up.
Return sections 9 and 10 of the report template as text, verbatim, in your report; do not write files: the proven bug, with its scenario, the test code and the failing output; and the suspects not proven, each with the test you tried and why it passed or could not run. Then section 11, what the audit could not check.
Never edit the audited branch or its worktree, never commit, never fix. End with one line: the proven bug and its severity, or that no bug was proven and how many suspects you tried.
```

Append each one's sections to its `audit-<n>.md`. When one auditor proves a P0, the parent stops the others: the audit needs one bug. A proven P1 does not stop the others, since one of them may still prove a P0.

For each proof worktree, save the proof test, new files included, with `git -C <dir>/proof-<n> add -A` and then `git -C <dir>/proof-<n> diff --cached` to `<dir>/proof-<n>.diff`, so a fix can take it as its regression test. Then remove the worktree with `git worktree remove --force <dir>/proof-<n>`.

Remove every proof worktree, also one whose auditor was stopped, failed or never ran. Then run `git worktree prune` and compare `git worktree list` with the list noted before phase 3; tell the user about any worktree left over.

### 5. Hand over

The parent picks the one bug: the P0 if one was proven, else a P1, else none. It writes `<dir>/audit.md`: first the three auditors' approaches, one line each, then that bug, the auditor that proved it, its red test and the path to its `proof-<n>.diff`, then the test gaps the three reports share, and links to the three reports.

The parent tells the user the bug, with its red test, and the test gaps before the user reads the code, and logs the bug as a work item in the task file, or in its report when it has no task file. When no bug was proven, it says so, with the number of suspects the three auditors attacked. The audit's report path goes in the Work log entry.

## Report

`references/report-template.md` is each auditor's report shape. Keep its sections and order: the first four prove the auditor committed before it saw the claims, and sections 9 and 10 show the suspects were attacked with tests. The user can tell a real audit from a rubber stamp by reading them.

## Smells

`references/smells.md` lists the mistakes this project has seen an agent make, one line each, with an example. It is a set of hints, not a checklist:

- The auditors read it in phase 2 only, after their own blind read, so it cannot steer where they look first.
- A smell that matches is a suspect to prove, not a bug. A bug that matches no smell is just as real; most bugs will not be on the list.
- An auditor that finds no smell has not finished: the list covers only what earlier reviews caught.

Add a line whenever the user's own read catches something the audit missed.
