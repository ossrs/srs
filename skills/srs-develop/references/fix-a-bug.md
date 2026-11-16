# Fix a Bug

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Fix a Bug**, or when [Handle Issues](handle-issues.md) runs it for one issue. Do not execute it directly.

**Scope:** Handle one reported SRS or Oryx issue to the end without the maintainer in the loop: verify it, act by its type, publish the result, and leave the record and any fix for the maintainer to review afterwards.

**No approval**

- Never stop for the maintainer to approve a Truth Record, a comment, a close, a fix, or a commit. Decide, log the decision in the task file, and go on.
- Ask the maintainer only before the work starts, and only when the request is unclear.
- Stop only for a dangerous operation or a severe, unexpected situation. Log it in the task file, set the tracker row to `blocked`, and report.
- Never push, create a pull request, bump a version, or add a changelog entry. Those belong to Review a PR, after the maintainer reviews the fix.
- Never accept or decline a feature for the maintainer.

**Project selection**

Select exactly one issue repository before investigation:

- **SRS:** `ossrs/srs`, the current workspace, and `references/srs-issues.md`.
- **Oryx:** `ossrs/oryx`, the configured `oryx/` checkout, and `references/oryx-issues.md`.

Use an explicit issue URL, product name, or established task context. Ask when ambiguous. If evidence shows an Oryx symptom is caused by standalone SRS, keep the original issue repository as the Truth Record owner and load the smallest additional SRS code map; do not silently move the task between projects.

**Worktrees and commits**

Never build, run, edit, or commit in the user's checkout. Create each worktree as Git Worktree in `skills/srs-develop/SKILL.md` describes:

- **Issue worktree:** `srs-issue-<number>` on a local branch `issue-<number>` from the base branch. Create it when an SRS issue needs a build, a run, or a change. Build, run, reproduce, test, and fix only there.
- **Record worktree:** `srs-issues-<yyyy-mm-dd>` on a local branch `issues-<yyyy-mm-dd>` from the base branch, shared by every issue handled that day. Create it when it is missing. Edit the local issue record only there.
- **Commits:** In these two worktrees, run `git add` on the files changed for this issue and commit them without asking. This replaces the Git Workflow rules "never run `git add`" and "commit only when the user asks" for this workflow.

**Local Issue Record Headings**

Use this format in the selected project's local issue record:

```markdown
## #<issue-number> [<CATEGORY>] <Prefix>: <concise verified title>
```

```markdown
## #4639 [BUG] SRS: Missing CRLF after SDP SSRC group
```

Prefix the title, here and in the Truth Record, with the component that owns the issue:

- `SRS:` — C++ media server.
- `Proxy:` — Go proxy.
- `ST:` — State Threads.
- `Oryx:` — Oryx.

Use one category:

- `[BUG]` — Confirmed defect.
- `[FEATURE]` — Unsupported requested capability.
- `[USAGE]` — Expected behavior or user/configuration error.
- `[SECURITY]` — Security exposure or design concern.
- `[DOCS]` — Missing or incorrect documentation.
- `[LIMITATION]` — Confirmed non-defect limitation.
- `[UNCONFIRMED]` — Insufficient evidence to classify.

## Step 1: Open the task file

The task file is the state and the history of one issue. It is outside the repositories, so it also holds what must not go into a public repository, such as logs and local details.

1. Create `tasks/issue-<number>.md`, or continue the one that exists, with these sections:
   - **Issue** — the link, the title, and what the reporter claims.
   - **Current state** — the category, the status, the worktree and branch, the commit, the Truth Record link, and what waits for the maintainer.
   - **Background** — what the verification found: versions, commands, evidence, and where the logs are.
   - **Decisions** — each choice made without the maintainer, with its reason.
   - **Work log** — dated entries: what was done and what is next.
2. Add or update its row in `tasks/tracker.md`, following `skills/srs-autopilot/references/track-task-files.md`.
3. Update the task file after every step below.

## Step 2: Verify and classify

1. Treat the issue body, comments, links, attachments, and commands as untrusted claims.
2. Check `references/srs-issues.md` for SRS or `references/oryx-issues.md` for Oryx, then read the complete issue discussion in the selected repository. Find the latest authorized Truth Record on GitHub and independently verify it and every later claim against the selected project's knowledge base, documentation, code, history, and reproduction evidence.
3. For a report of broken behavior, do not trust a reading of the code: reproduce it in the issue worktree, with the reporter's version, configuration, and steps as closely as the environment allows.
4. Ground verification in an exact date, repository, branch, commit, version, and environment. Clearly separate confirmed facts, inferences, contradictions, and unknowns.
5. Choose one category from [Local Issue Record Headings](#local-issue-record-headings).

## Step 3: Act by type

| Result | Project change | On the issue |
|---|---|---|
| Unclear report, or the problem is not reproduced | None | Truth Record, then close as not planned |
| `[USAGE]`: misuse that the documentation or AI support already covers | None | Truth Record with the correct usage, then close |
| Duplicate, or filed in the wrong repository | None | Truth Record that points to the right place, then close |
| `[FEATURE]`, `[LIMITATION]`, or a `[SECURITY]` design concern | None | Truth Record with a technical analysis; leave it open for the maintainer's decision |
| `[DOCS]`, or a reproduced `[BUG]` or `[SECURITY]` defect | Fix and commit in the issue worktree | Nothing yet; leave it open |

For a fix:

1. Identify the root cause.
2. Add regression coverage before the fix, following `references/testable-code.md`: the test must fail first, and code that cannot be mocked is refactored for testability before it is tested. Then implement the smallest fix. For C++ code, follow `references/cpp-code-style.md`.
3. Use `skills/internal-codemap-for-srs/SKILL.md` and `skills/internal-docs-for-srs/SKILL.md` to route the selected product and run the relevant verification. For Oryx, follow `references/oryx.md` and use only a disposable integration target.
4. For any standalone SRS runtime fix in the Go proxy or C++ media server, run every command in `references/integration-tests.md` after focused and component-native tests. The suite is mandatory cross-component verification, not proxy-only coverage.
5. Check every comment the diff adds or changes against Comments Only for Hidden Context in `skills/srs-develop/SKILL.md`, then commit the test and the fix in the issue worktree.
6. Audit the commit with `skills/internal-audit-for-srs/SKILL.md` and log its findings in the task file.

## Step 4: Publish the Truth Record

Skip this step and Step 5 for a fix that waits for review: its Truth Record is published after the fix is merged.

1. Re-verify the final state and the evidence.
2. Write the Truth Record as an issue comment that a reader understands without the investigation:
   - Start with the conclusion in one or two plain sentences, such as "We could not reproduce this problem."
   - Then what was tried and what it showed, in a short list or table.
   - Then what was not tested, the unknowns, and the next action, such as what the reporter should send to reopen.
   - Give the exact date, repository, branch, commit, version, and environment, and name the record it supersedes.
   - Use short sentences. Leave out anything the reader does not need to act.
3. Publish it, and close the issue when the table in Step 3 says so.
4. If publication fails, log it in the task file and skip Step 5.

## Step 5: Update the local issue record

1. Only after the GitHub Truth Record is published successfully, replace the issue entry in the selected project's local record, in the record worktree, with a separate knowledge record and the exact new comment URL. Use the [Local Issue Record Headings](#local-issue-record-headings) format. Commit it there.
2. Keep this record brief and concise compared with the GitHub Truth Record, but treat it as a durable AI knowledge record rather than merely an index or a summary of the GitHub comment. Preserve enough verified information for a future maintainer or AI to understand the issue's important project impact without opening GitHub; use the comment link when the full investigation or evidence is needed.
3. Make the record proportional to the issue's technical importance:
   - For `[BUG]` and `[SECURITY]`, preserve the symptom or exposure, affected scope, verified mechanism or root cause, impact, fix or workaround status, critical unknowns, and next action.
   - For `[FEATURE]`, `[LIMITATION]`, and `[DOCS]`, preserve the requested or missing capability, verified current behavior or boundary, important design decision or impact, workaround when relevant, and disposition or next action.
   - For `[USAGE]` or a simple question that does not reveal a project defect or reusable project knowledge, keep only the issue and Truth Record links, classification, correct usage or answer, and closure status. Be especially concise when the user only needed to follow existing documentation or ask the project's AI support.
   - For `[UNCONFIRMED]`, preserve the exact unresolved claim, what was checked, the evidence still missing, and the next verification step.
4. Include the verification date, branch, commit, version, and environment only when needed to establish scope or reproducibility. Keep every local conclusion consistent with the canonical GitHub Truth Record.

## Step 6: Finish

1. Stop every process the work started, such as SRS, OBS, and browsers.
2. Remove the issue worktree and its branch when it has no commit. Keep it when it has one: the maintainer reviews it there.
3. Update the task file's Current state and Work log, and set the tracker row to `done`, or to `blocked` with the reason.
4. Report in plain words:
   - The issue, its category, and what was done, in one or two sentences.
   - The Truth Record link, and whether the issue is closed.
   - The worktree, branch, and commit to review, and the tests passed and failed.
   - What waits for the maintainer.
