# Review a PR

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Review a PR**. Do not execute it directly.

**Scope:** Walk pending changes in one selected SRS or Oryx repository, summarize them relative to the actual pull-request base, sync stale navigation docs, then apply that project's version and changelog rules once the user supplies the PR number.

**Guiding rules**
- **Docs are navigation, not tutorials.** When a code change makes an entry stale, *correct* it — don't expand it. Only *add* a new entry when a new file or module was introduced; never to describe a refactor inside an existing module.

## Step 0: Select the repository and base

1. Select exactly one repository from the user's PR URL, product name, or established task context:
   - SRS: current workspace.
   - Oryx: `~/git/oryx`, accessed through `git -C` without changing the current working directory.
2. Ask when the product is ambiguous. If the selected Oryx checkout is missing, ask the user to clone `https://github.com/ossrs/oryx` into `~/git/oryx`.
3. Determine the actual PR base from the pull request when available. Otherwise use `develop` for SRS and `origin/main` for Oryx unless the branch clearly tracks another base.
4. Inspect the selected repository's status, branch, commit, and remotes. Do not overwrite unrelated work or mix diffs from both repositories.

## Step 1: Survey the changes

1. Run the selected repository's diff-stat and log relative to the confirmed base. Use `git -C ~/git/oryx` for Oryx.
2. Drill into non-test source diffs relative to that base to understand what actually changed.
3. Summarize back to the user: refactors, new files, and anything that could break downstream consumers (log format, public API, wire format, etc.).
4. Pause and let the user redirect or ask for more detail.

## Step 2: Correct stale navigation docs

1. Load `skills/internal-codemap-for-srs/SKILL.md`, route to the selected product's smallest code map, and check the entries covering each module touched in this PR. Use `references/oryx.md` for Oryx.
2. For each entry whose description is no longer accurate, make the **smallest** correction needed to match the new code. Keep the one-line summary style; do not expand into implementation detail.
3. Stop and let the user review and stage the files they accept. After an explicit commit request, use a repository-appropriate message such as `<Tool>: Sync internal code map for <component>.`.

## Step 3: Apply project version and changelog rules

1. Ask the user for the PR number if they haven't given it.
2. For SRS, bump revision by one in **both** version files, keeping them in sync:
   - `internal/version/version.go` — `VersionRevision()`
   - `trunk/src/core/srs_core_version8.hpp` — `VERSION_REVISION`
3. For SRS, add a new top entry to `trunk/doc/CHANGELOG.md` under `## SRS 8.0 Changelog`, matching the existing format:
   ```
   * v8.0, YYYY-MM-DD, Merge [#PR](URL): <Prefix>: <one-line summary>. v8.0.<rev> (#PR)
   ```
   Propose the summary to the user; don't invent one unilaterally.
4. For Oryx, inspect `platform/version.go`, the newest entries under `DEVELOPER.md#changelog`, tags, and branch history before proposing a version. Do not assume the version constant and newest changelog entry are already synchronized.
5. When the maintainer approves an Oryx version update, change `platform/version.go` and add the smallest matching entry under the current series in `DEVELOPER.md`. Do not change `releases/version.go`; its legacy `latest`, `api`, and `stable` values are a separate compatibility service unless the PR explicitly changes that service.
6. Do not force a version bump for Oryx documentation, skill, issue-template, or maintenance-only work when the maintainer does not intend a release. Ask rather than infer.
7. Stop and let the user review and stage the version and changelog files. After an explicit commit request, follow `SKILL.md`'s repository-aware Git Workflow.
