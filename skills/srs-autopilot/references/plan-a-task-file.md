# Plan a Task File

The goal is a plan the loop can run as long as possible without the user.

1. Understand the problem before planning. Work out with the user what background it needs, and research it in the SRS code and docs, RFCs, and the web.
2. Discuss and confirm with the user, one by one: scope, constraints, special requirements, decisions, and what to test.
3. Write `tasks/<topic>.md` with these sections:
   - **Goal and scope** — what is in and out.
   - **Repositories** — a table of every repository the task changes or tests, such as SRS, State Threads, or Oryx, with its branch and its worktree on each machine, as `~/` paths, not absolute paths. The project-root symlinks such as `state-threads/` and `oryx/` point at the main checkouts, so say not to use them.
     - When planning, create a worktree and branch for the task in each of these repositories, with the same topic suffix: sibling `~/projects/<repo>-<topic>` on branch `<topic>`.
     - Always create one in SRS too, since the task's docs and skills live there. For example, an ST task gets `state-threads-qemu` and `srs-qemu`; an SRS-only task gets `srs-integ`.
   - **Background** — what the research found, with links.
   - **Current state** — a short table, and the next task.
   - **Review** — the review setup and a commits table; see [Review Commits](review-commits.md).
   - **Decisions** — decided (with date) and open questions.
   - **Phases and tasks** — small tasks with IDs (`P1.1`), marks `[ ]` / `[~]` / `[x]`, and an exit criterion and tests per phase.
   - **Work log** — dated entries: what changed, commit, verified, next.
4. Resolve every open question with the user and record it as a decision, so the plan has none before it runs.
5. Fix every command each task runs. A multi-line shell blob, such as `bash -c '...'`, needs the user's permission and stalls the loop, so:
   - Write each check or test that needs more than one plain command as a script in `srs-develop` `scripts/`, in the task's SRS worktree, and run it once.
   - Name the scripts and commands in each task, so a subagent only runs them.
6. Add the task file's row to the tracker, `draft` while questions remain and `ready` once none do; see [Track Task Files](track-task-files.md).
