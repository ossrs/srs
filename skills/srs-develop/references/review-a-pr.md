# Review a PR

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Review a PR**. Do not execute it directly.

**Scope:** Walk the pending changes on the current branch (relative to `develop`), summarize them, sync any stale navigation docs, then bump the version and add a changelog entry once the user supplies the PR number.

**Guiding rules**
- **Docs are navigation, not tutorials.** When a code change makes an entry stale, *correct* it — don't expand it. Only *add* a new entry when a new file or module was introduced; never to describe a refactor inside an existing module.

## Step 1: Survey the changes

1. Run `git diff develop --stat` and `git log develop..HEAD --oneline` to get the shape of the branch.
2. Drill into non-test source diffs with `git diff develop -- <path>` to understand what actually changed.
3. Summarize back to the user: refactors, new files, and anything that could break downstream consumers (log format, public API, wire format, etc.).
4. Pause and let the user redirect or ask for more detail.

## Step 2: Correct stale navigation docs

1. Load `skills/internal-codemap-for-srs/SKILL.md`, route to the next-generation Go server map, and check the entries covering each module touched in this PR.
2. For each entry whose description is no longer accurate, make the **smallest** correction needed to match the new code. Keep the one-line summary style; do not expand into implementation detail.
3. Stop and let the user review and stage the files they accept. After an explicit commit request, use a short message such as `<Tool>: Sync internal Go code map with internal/<modules>.`.

## Step 3: Bump the version and update the changelog

1. Ask the user for the PR number if they haven't given it.
2. Bump revision by one in **both** version files, keeping them in sync:
   - `internal/version/version.go` — `VersionRevision()`
   - `trunk/src/core/srs_core_version8.hpp` — `VERSION_REVISION`
3. Add a new top entry to `trunk/doc/CHANGELOG.md` under `## SRS 8.0 Changelog`, matching the existing format:
   ```
   * v8.0, YYYY-MM-DD, Merge [#PR](URL): <Prefix>: <one-line summary>. v8.0.<rev> (#PR)
   ```
   Propose the summary to the user; don't invent one unilaterally.
4. Stop and let the user review and stage the version files and changelog. After an explicit commit request, use a short message such as `<Tool>: Bump to v8.0.<rev> for #<PR>.`.
