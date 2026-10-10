# Run a Task File

The main agent never reads the task file or implements tasks. It loops:

1. Start one new subagent with the task prompt below. Before the first one, set the task file's tracker row to `running`; see [Track Task Files](track-task-files.md).
2. Check the report and that the repository is clean with a new commit.
3. Start one new subagent with the summary prompt below, so the totals stay out of the main agent's context. Report its summary to the user.
4. Stop only when the subagent needs the user, is blocked, all tasks are done, or the user asked to pause. Otherwise go to step 1. If the user asked to pause, set the tracker row to `paused`; the subagent sets the other states.

Task prompt:

```
Do exactly one task of tasks/<topic>.md.
Read the task file in full; it is the plan, the rules, and the state. Pick the first unfinished task.
Decide open choices yourself: pick the best option and log it as a decision. Stop and report only for a dangerous operation or a severe, unexpected situation.
Follow the srs-autopilot skill's git rules, its references/track-task-files.md for the tracker row, and the srs-develop skill for the work.
Run only plain commands and script files; never a multi-line shell blob such as bash -c '...'. If the task needs a new check, write it as a script file.
Mark the task [~], write tests first, implement, and run the tests until they pass.
Commit, add a todo row for the commit, with its commit time, at the end of the Review commits table, tick the task [x], update Current state, add a Work log entry, update the task file's row in tasks/tracker.md (progress, next task, date, and `done` if all tasks are done), then quit.
If blocked, do not commit; leave the task [~], log the blocker, set the tracker row to `blocked` (or `paused` if it needs the user), and report.
Report: the task ID, the start and end time, the commit hash, the files changed and lines added and removed, the tests passed and failed by type (such as utest, integration tool, script, or E2E) and by OS and CPU (such as macOS arm64, Linux arm64 in Docker, Windows x64), the OSes not tested and why, and anything blocked or for the user, or that all tasks are done.
```

Summary prompt:

```
Summarize this run of tasks/<topic>.md so far from the task reports below, and check the numbers against git.
Report the task just finished, then the totals so far: the start and end time, the tasks finished and left, the commits, the files changed, the lines added and removed, the tests passed and failed by type and by OS and CPU, the OSes not tested, and anything blocked or for the user.
<the task reports>
```
