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

## Step 1: Survey the changes

1. Run the selected repository's diff-stat and log relative to the confirmed base. Use `git -C oryx/` for Oryx.
2. Drill into non-test source diffs relative to that base to understand what actually changed.
3. Summarize back to the user: refactors, new files, and anything that could break downstream consumers (log format, public API, wire format, etc.).
4. Pause and let the user redirect or ask for more detail.

## Step 2: Correct stale navigation docs

1. Load `skills/internal-codemap-for-srs/SKILL.md`, route to the selected product's smallest code map, and check the entries covering each module touched in this PR. Use `references/oryx.md` for Oryx.
2. For each entry whose description is no longer accurate, make the **smallest** correction needed to match the new code. Keep the one-line summary style; do not expand into implementation detail.
3. Stop and let the user review and stage the files they accept. After an explicit commit request, use a repository-appropriate message such as `<Tool>: Sync internal code map for <component>.`.

## Step 3: Verify runtime changes

1. For a standalone SRS runtime PR affecting either the Go proxy or C++ media server, load the applicable testing map and run focused and component-native verification.
2. Run every command in `references/integration-tests.md` sequentially. The bundled suite is required cross-component verification even when the PR changes only the C++ media server; its `proxy-*` filenames do not make it proxy-only.
3. Record every result and exact environmental blocker. Do not claim the PR is fully verified while any required script is skipped or blocked.
4. For Oryx, documentation-only, skill-only, issue-template, or packaging-only PRs, follow the owning workflow's verification instead of this standalone SRS runtime suite.

## Step 4: Apply project version and changelog rules

1. Ask the user for the PR number if they haven't given it.
2. Load `references/version-and-changelog.md` and apply it. It is the single owner of the version-bump and changelog rules for both projects, including the requirement to keep the two SRS version files in sync.
