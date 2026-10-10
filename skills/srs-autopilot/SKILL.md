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
- Never `git push`. The one exception is `srs-develop` `scripts/st-windows-test.sh`: to test on another OS, it pushes the commit to its branch's upstream, such as a personal fork, only to sync the branch to the test host. It never pushes to `origin`.
- Never commit the task file or the tracker; `tasks/` is outside the repositories.
- Never touch the review branch or worktree while running tasks; only a review ([Review Commits](#review-commits)) changes them.
- Decide on your own: when unsure, choose the best option, log it as a decision, and go on. Stop and ask the user only for a dangerous operation or a severe, unexpected situation.

## Workflows

Each workflow lives in its own reference file. Load only the file for the workflow the user asked for, and follow it. Resolve `references/` paths relative to the directory containing this `SKILL.md`.

### Plan a Task File

Use when the user asks to plan a large task or write a task file. → Load [references/plan-a-task-file.md](references/plan-a-task-file.md).

### Track Task Files

Use whenever a task file's row in `tasks/tracker.md` is added or updated, or when the user asks to list the tasks. Planning and running a task file both update the tracker. → Load [references/track-task-files.md](references/track-task-files.md).

### Run a Task File

Use when the user asks to run or resume a task file, such as "run ./tasks/xxx.md". → Load [references/run-a-task-file.md](references/run-a-task-file.md).

### Review Commits

Use when the user asks to review a task file's commits, such as "review ./tasks/xxx.md". → Load [references/review-commits.md](references/review-commits.md).
