---
name: srs-autopilot
description: Plan and run a complex, multi-step SRS, Oryx, or State Threads task through a task file in `tasks/`. The main agent loops one subagent per task; each subagent implements, tests, and commits one task, then the loop continues until all tasks are done. It is autonomous, so no human review blocks the loop. Use when the user asks to plan a large task, write a task file, or run or resume one (for example "run ./tasks/xxx.md").
---

# SRS Autopilot

A task file is the plan, the state, and the log of one complex task. The main agent orchestrates; subagents do the work. The user reviews the work through the git history and does not block the loop.

## Skill Dependencies

- `skills/srs-develop/SKILL.md` — product background when planning, and every subagent follows it for its one task (Task Router, TDD, tests, commit format).

## Git Rules

These override the `srs-develop` git rules for tasks run by this skill:

- When a task's tests pass, the subagent runs `git add` on the files it changed and commits them in the owning repository. One commit per task.
- Never `git push`.
- Never commit the task file; `tasks/` is outside the repositories.
- When unsure, or a change is risky or needs the user's review, do not commit; stop the loop and ask the user.

## Plan a Task File

The goal is a plan the loop can run as long as possible without the user.

1. Understand the problem before planning. Work out with the user what background it needs, and research it in the SRS code and docs, RFCs, and the web.
2. Discuss and confirm with the user, one by one: scope, constraints, special requirements, decisions, and what to test.
3. Write `tasks/<topic>.md` with these sections:
   - **Goal and scope** — what is in and out.
   - **Background** — what the research found, with links.
   - **Current state** — a short table, and the next task.
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
Commit, tick the task [x], update Current state, add a Work log entry, then quit.
If blocked, do not commit; leave the task [~], log the blocker, and report.
Report: the task ID, the start and end time, the commit hash, the files changed and lines added and removed, the tests passed and failed by type (such as utest, integration tool, script, or E2E), and anything blocked or for the user, or that all tasks are done.
```

Summary prompt:

```
Summarize this run of tasks/<topic>.md so far from the task reports below, and check the numbers against git.
Report the task just finished, then the totals so far: the start and end time, the tasks finished and left, the commits, the files changed, the lines added and removed, the tests passed and failed by type, and anything blocked or for the user.
<the task reports>
```
