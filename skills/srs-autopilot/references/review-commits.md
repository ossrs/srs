# Review Commits

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
