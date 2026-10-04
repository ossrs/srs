---
name: srs-autopilot
description: Plan and run a complex, multi-step SRS, Oryx, or State Threads task through a task file in `tasks/`. The main agent loops one subagent per task; each subagent implements, tests, and commits one task, then the loop continues until all tasks are done. It is autonomous, so no human review blocks the loop. Use when the user asks to plan a large task, write a task file, run or resume one (for example "run ./tasks/xxx.md"), or review its commits (for example "review ./tasks/xxx.md").
---

# SRS Autopilot

A task file is the plan, the state, and the log of one complex task. The main agent orchestrates; subagents do the work. The user reviews the commits on a review branch, at the same time as the loop runs, and does not block it.

## Skill Dependencies

- `skills/srs-develop/SKILL.md` — product background when planning, and every subagent follows it for its one task (Task Router, TDD, tests, commit format).

## Git Rules

These override the `srs-develop` git rules for tasks run by this skill:

- When a task's tests pass, the subagent runs `git add` on the files it changed and commits them in the owning repository. One commit per task.
- Never `git push`.
- Never commit the task file; `tasks/` is outside the repositories.
- Never touch the review branch or worktree while running tasks; only a review (below) changes them.
- When unsure, or a change is risky or needs the user's review, do not commit; stop the loop and ask the user.

## Plan a Task File

The goal is a plan the loop can run as long as possible without the user.

1. Understand the problem before planning. Work out with the user what background it needs, and research it in the SRS code and docs, RFCs, and the web.
2. Discuss and confirm with the user, one by one: scope, constraints, special requirements, decisions, and what to test.
3. Write `tasks/<topic>.md` with these sections:
   - **Goal and scope** — what is in and out.
   - **Background** — what the research found, with links.
   - **Current state** — a short table, and the next task.
   - **Review** — the review setup and a commits table; see [Review Commits](#review-commits).
   - **Decisions** — decided (with date) and open questions.
   - **Phases and tasks** — small tasks with IDs (`P1.1`), marks `[ ]` / `[~]` / `[x]`, and an exit criterion and tests per phase.
   - **Work log** — dated entries: what changed, commit, verified, next.
4. Resolve every open question with the user and record it as a decision, so the plan has none before it runs.

## Run a Task File

The main agent never reads the task file or implements tasks. It loops:

1. Start one new subagent with the task prompt below.
2. Check the report and that the repository is clean with a new commit.
3. Start one new subagent with the summary prompt below, so the totals stay out of the main agent's context. Report its summary to the user.
4. Stop only when the subagent needs the user, is blocked, all tasks are done, or the user asked to pause. Otherwise go to step 1.

Task prompt:

```
Do exactly one task of tasks/<topic>.md.
Read the task file in full; it is the plan, the rules, and the state. Pick the first unfinished task.
If it needs the user (an open decision, installing software, or anything the task says to ask about), do not start it; report the question.
Follow the srs-autopilot skill's git rules and the srs-develop skill for the work.
Mark the task [~], write tests first, implement, and run the tests until they pass.
Commit, add a todo row for the commit, with its commit time, at the end of the Review commits table, tick the task [x], update Current state, add a Work log entry, then quit.
If blocked, do not commit; leave the task [~], log the blocker, and report.
Report: the task ID, the start and end time, the commit hash, the files changed and lines added and removed, the tests passed and failed by type (such as utest, integration tool, script, or E2E), and anything blocked or for the user, or that all tasks are done.
```

Summary prompt:

```
Summarize this run of tasks/<topic>.md so far from the task reports below, and check the numbers against git.
Report the task just finished, then the totals so far: the start and end time, the tasks finished and left, the commits, the files changed, the lines added and removed, the tests passed and failed by type, and anything blocked or for the user.
<the task reports>
```

## Review Commits

The user reviews each commit by cherry-picking it to a review branch and checking it there. The **Review** section of the task file holds the state, so a review can stop and resume, and the loop can add commits meanwhile.

The Review section has:

- **Setup** — the source branch and worktree, the base commit, the review branch (from the base) and its worktree, and the cherry-pick command: `git cherry-pick <source>` without `-x`, so the message stays the same.
- **Reviewing** and **Next** — the commit under review and the one after it.
- **Commits** — one row per commit, oldest first: `#`, task ID, source hash, source time, picked hash, picked time, status, and a short subject. Times are the commit times on each branch, local `YYYY-MM-DD HH:MM:SS` (`git log --date=format:'%Y-%m-%d %H:%M:%S' --format=%cd`). Status is `todo`, `picked` (on the review branch, under review), `reviewed` (accepted, maybe with fixes), or `dropped`. If a source commit is amended, update its hash and time.

A review runs in its own session, separate from the loop, so both can run at once. The review session changes only the review worktree and the task file's Review section and Work log. The loop only appends `todo` rows, so re-read the task file before each edit. The review session does these steps:

1. **Set up**, the first time: create the review branch and worktree, write the Review section, and add a row for every commit so far.
2. **Pick** the oldest `todo` commit (commits build on each other, so review them in order), record its picked hash and time, and mark it `picked`. On a conflict, stop and ask the user.
3. **Explain.** Load the context with the `srs-develop` skill: the commit message and diff, its task, decisions, and Work log entry in the task file, and the code around the change. Then explain it as if the user knows nothing: the background, what the commit changes and why, and how it was tested.
4. **Check later commits.** Use a subagent, so the diffs stay out of the review session's context; it reports only the result, or that it found nothing. It finds the later commits on the source branch that change the same files (`git log <source>..<source branch> -- <files>`) and reports which of them change the same places again. The review session tells the user, so a review fix does not repeat or conflict with later work.
5. **Run the tests.** Start a subagent that runs all the tests in the review worktree (`srs-develop` for how) and reports passed and failed. It runs while the user reads the code. A failure does not block: an early commit may fail until a later one completes the fix, so report it and let the user decide.
6. **Wait for the user.** Accept marks it `reviewed`. Fixes go in on the review branch as the user says (amend or a new commit), then `reviewed`. Drop resets the pick and marks it `dropped`.
7. **Log.** Add a Work log entry, update Reviewing and Next, tell the user this commit is finished, and stop. The review is manual: do not pick the next commit until the user asks.
