# Review a PR

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Review a PR**. Do not execute it directly.

**Scope:** Walk pending changes in one selected SRS or Oryx repository, summarize them relative to the actual pull-request base, sync stale navigation docs, then apply that project's version and changelog rules once the user supplies the PR number.

**Guiding rules**
- **Docs are navigation, not tutorials.** When a code change makes an entry stale, *correct* it — don't expand it. Only *add* a new entry when a new file or module was introduced; never to describe a refactor inside an existing module.

## Step 0: Select the repository and base

1. Select exactly one repository from the user's PR URL, product name, or established task context:
   - SRS: current workspace.
   - Oryx: `oryx/`, accessed through `git -C` without changing the current working directory.
2. Ask when the product is ambiguous. If the project-root-relative `oryx/` path is unavailable, ask the user to make the `https://github.com/ossrs/oryx` checkout available there directly or through a symlink.
3. Determine the actual PR base from the pull request when available. Otherwise use `develop` for SRS and `origin/main` for Oryx unless the branch clearly tracks another base.
4. Inspect the selected repository's status, branch, commit, and remotes. Do not overwrite unrelated work or mix diffs from both repositories.
5. For an SRS pull request, fetch its head into a local branch `pr-<number>` and create the worktree `srs-pr-<number>` on it, as Git Worktree in `skills/srs-develop/SKILL.md` describes. Run every later step in that worktree.
   - Make `pr-<number>` track the PR's head branch, so a push updates the PR. Read the head repository and branch with `gh pr view <number> --json headRepository,headRefName,maintainerCanModify`, add a remote named after the head repository's owner when it is missing, and fetch that branch.
   - Tell the user when `maintainerCanModify` is false: a push to the head branch is refused.

## Step 1: Survey the changes

1. Run the selected repository's diff-stat and log relative to the confirmed base. Use `git -C oryx/` for Oryx.
2. Drill into the non-test code and doc diffs relative to that base to understand what actually changed.
3. From those code and doc changes, tell the user the idea of the PR first: the problem it fixes or the feature it adds, and how. The PR description and discussion are claims to check against them, not the source.
4. Audit the change with `skills/internal-audit-for-srs/SKILL.md` on `<base>..<tip>`, and give the user its findings and test gaps before the summary.
5. Summarize back to the user: refactors, new files, and anything that could break downstream consumers (log format, public API, wire format, etc.).
6. Pause and let the user redirect or ask for more detail.

## Step 2: Reproduce and fix the problem on the base

For a PR that fixes a bug, prove the problem exists before the fix is trusted. The PR worktree already has the fix, so a test written there passes and proves nothing.

1. In the SRS checkout on the base branch, without the PR's change, write a unit test that reproduces the problem, following the red phase in `references/testable-code.md`. It asserts the correct behavior, so it fails on the base.
2. Run it and show the user the failure. Pause until the user confirms it fails for the problem the PR fixes.
3. Fix the problem in the same checkout, run the test, and show the user that it passes. After the user reviews them, commit the test and the fix.
4. Merge the base branch, at its commit before the fix, into the PR branch in the worktree, since the PR may be behind it. Resolve any conflicts, show the user each resolution, and commit the merge after the user reviews it.
5. Cherry-pick that commit into the PR worktree, resolve conflicts with the PR's change, run the test there, and show that it passes. Then continue with the next step in the worktree.

## Step 3: Correct stale navigation docs

1. Load `skills/internal-codemap-for-srs/SKILL.md`, route to the selected product's smallest code map, and check the entries covering each module touched in this PR. Use `references/oryx.md` for Oryx.
2. For each entry whose description is no longer accurate, make the **smallest** correction needed to match the new code. Keep the one-line summary style; do not expand into implementation detail.
3. Stop and let the user review and stage the files they accept. After an explicit commit request, use a repository-appropriate message such as `<Tool>: Sync internal code map for <component>.`.

## Step 4: Verify runtime changes

1. For a standalone SRS runtime PR affecting either the Go proxy or C++ media server, load the Tests and Verification section of the routed code map and run focused and component-native verification.
2. Run every command in `references/integration-tests.md` sequentially. The bundled suite is required cross-component verification even when the PR changes only the C++ media server; its `proxy-*` filenames do not make it proxy-only.
3. Record every result and exact environmental blocker. Do not claim the PR is fully verified while any required script is skipped or blocked.
4. For Oryx, documentation-only, skill-only, issue-template, or packaging-only PRs, follow the owning workflow's verification instead of this standalone SRS runtime suite.

## Step 5: Apply project version and changelog rules

1. Ask the user for the PR number if they haven't given it.
2. Load `references/version-and-changelog.md` and apply it. It is the single owner of the version-bump and changelog rules for both projects.

## Step 6: Update or comment on the PR

The user's decision on the PR picks what to do:

- **Merge:** after the problem is confirmed and the version and changelog are bumped, rewrite the PR title and description from what the review learned: the problem, the fix, and the verification. Use the changelog summary and version as the title, such as `RTC: Fix negative SDP session-id on Android. v8.0.67`. Show the user the draft, then update the PR with `gh pr edit <number> --repo <owner/repo> --title <title> --body-file <file>`.
- **Close or give up:** comment on the PR with the reason, as below.

To comment:

1. Only when the user decides it. Never post or edit a PR comment on your own initiative.
2. Post one comment with `gh pr comment <number> --repo <owner/repo> --body-file <file>`. Keep it to the findings the user asked for (e.g. Background/Current state/Conclusion); drop metadata sections meant for issue Truth Records.
3. To revise it later instead of duplicating, use `gh pr comment <number> --repo <owner/repo> --edit-last --body-file <file>`.
