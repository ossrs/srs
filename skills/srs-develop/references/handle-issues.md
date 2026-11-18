# Handle Issues

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Handle Issues**. Do not execute it directly.

**Scope:** Handle a batch of issues in one repository to the end without the maintainer in the loop, such as every issue that needs attention, overnight. The maintainer reviews the results afterwards. Creating a pull request is another workflow.

The main agent only orchestrates: it selects the issues, starts one subagent per issue, and reports. It never investigates an issue itself.

## Step 1: Select the issues

1. Select exactly one issue repository: `ossrs/srs` or `ossrs/oryx`. Ask when ambiguous; this is the only time to ask.
2. Take the issues the user named. Otherwise run [Scan Issues](scan-issues.md) with the requested count; without a count, scan every open issue.
3. Set aside an issue that already has `tasks/issue-<number>.md`, unless the issue has content or comments later than that file's last Work log entry.
4. Never skip a selected issue for its type or content, such as off-topic or a feature: Fix a Bug decides how to act on it, including closing it.

## Step 2: Handle them one by one

Run the issues one after another, never in parallel: their builds and test ports collide.

1. Start one new subagent for the next issue with the prompt below.
2. Check its report. When it is blocked, go on to the next issue.
3. When the issue worktree has commits, audit them yourself as one review group, from the base branch to the branch tip, with `skills/internal-audit-for-srs/SKILL.md`. Do not rely on an audit the subagent ran: it may have skipped commits or stopped early. When the audit proves a bug, send it to the issue's subagent to fix in the same worktree with the failing test, and report the fix commit as not audited.
4. Stop the batch only when every issue is handled, the user asks to pause, or the same failure blocks every issue, such as no GitHub access.

```
Handle <repository> issue #<number> to the end.
Load the srs-develop skill and follow its Fix a Bug workflow, references/fix-a-bug.md, from Step 1 to Step 6.
Never ask for approval: decide, log the decision in the task file, and go on.
End with the report of its Step 6.
```

## Step 3: Report

1. Check the reports against git, GitHub, and `tasks/tracker.md`: each worktree and commit exists, each comment is published, and each closed issue is closed.
2. Check that no process started by the batch still runs.
3. Report in plain words:
   - One table with a row per issue: the issue link, its category, what was done, the Truth Record link, the worktree and commit to review, the audit result, and what waits for the maintainer.
   - The record worktree and its commits.
   - The issues that are blocked, and why.

## Step 4: Prepare an issue for review

When the maintainer says they start reviewing an issue, prepare its branch in the issue worktree:

1. Squash all its commits into one, with `git reset --soft <base>` and a commit whose title and description cover every commit.
2. Rebase the commit onto the latest `develop`, since other issues may be merged meanwhile. Resolve any conflict, then rebuild and run the tests again.
3. Report the new commit and any conflict resolved.
4. Explain the issue to the maintainer as to a reader with no background on it, in plain words, building up from what they know:
   - How the feature works, with the terms it needs and a small diagram when it helps.
   - The bug as a story: what the user did, what the code did, and what the user saw.
   - Why the obvious fix is not enough, when it is not.
   - The fix in plain words, and its cost.
   - Where to read the code, with the few lines that matter, and the tests to read with it.
