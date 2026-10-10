# Track Task Files

`tasks/tracker.md` lists every task file in `tasks/` and its state, so the user sees all tasks in one place. Other skills add their task files to it too.

- **Row** — the owner, the task file, the goal in one line, the repositories and worktrees, the status, the progress (tasks done of total, and any `[~]`), the next task, and the date updated.
- **Owner** — this skill registers its rows with the owner `SRS`. Rows with another owner belong to other skills: leave them alone.
- **List** — when the user asks for the tasks, such as the active ones, show only the rows owned by `SRS`.
- **Status** — `draft`, `ready`, `running`, `paused`, `blocked`, `done`, or `dropped`. Finished task files move to the Finished table.
- **When** — add the row when a task file is created, and update it every time the task file's Current state changes: planning, each task, a blocker, a pause, and the end.
- **Concurrent edits** — several sessions edit the tracker, so re-read it before each edit and change only the row of your own task file.
