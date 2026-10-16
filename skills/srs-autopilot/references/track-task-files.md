# Track Task Files

`tasks/tracker.md` lists every task file in `tasks/` and its state, so the user sees all tasks in one place. Other skills add their task files to it too.

- **Row** — the owner, the task file, the goal in one line, the repositories and worktrees, the status, the progress, the next task, and the date updated.
- **Owner** — this skill registers its rows with the owner `SRS`. Rows with another owner belong to other skills: leave them alone.
- **List** — when the user asks for the tasks, such as the active ones, show only the rows owned by `SRS`.
- **Status** — `draft`, `ready`, `running`, `paused`, `blocked`, `done`, or `dropped`. Finished task files move to the Finished table.
- **When** — add the row when a task file is created, and update it every time the task file's Current state changes: planning, each task, a blocker, a pause, and the end.
- **Concurrent edits** — several sessions edit the tracker, so re-read it before each edit and change only the row of your own task file.

## A row stays the same size

The row points at the task file; it does not copy it. The task file's Current state and Work log hold the details, and the row holds only the state that changes. Each update replaces a cell; nothing is appended, so a row is as short after a hundred tasks as after one.

- **Goal** — one line, written when the row is added; the tasks do not change it.
- **Repositories and worktrees** — each repository's branch and worktree.
- **Status** — one of the states above.
- **Progress** — the tasks done of the total, such as `109 of 120`, and the ID of the task in progress if one is marked `[~]`. Nothing else: no commit hashes, no review numbers, no bug list, no history.
- **Next** — the next task's ID and title in a few words, and why it waits, if it does, in a few words.
- **Updated** — the date.
- **Finished table** — the goal, the status (`done` or `dropped`) with one short clause, such as the merge PR or what was left, and the date.

A row is under about 400 characters. If it needs more, the extra belongs in the task file. Nothing from a task report goes into the row: what a task changed, found or decided is logged in the task file only.
