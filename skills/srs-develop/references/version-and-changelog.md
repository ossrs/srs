# Version and Changelog Rules

**Scope:** The single owner of SRS and Oryx version-bump and changelog rules. Every workflow that bumps a version loads this file; do not restate these rules elsewhere.

Apply these rules whenever a task bumps a version or adds a changelog entry, regardless of which task the router selected. A version bump is not exclusive to the Review a PR workflow — Fix a Bug and Develop Code reach it too.

## SRS

**Bump revision by one in both version files, and keep them in sync. Missing either one is a defect.**

- `trunk/src/core/srs_core_version8.hpp` — `VERSION_REVISION`
- `internal/version/version.go` — `VersionRevision()`

The two files are separate products from the same release: the C++ media server and the Go proxy. Nothing in the build fails when they diverge, and no test catches it, so verify both by reading them after editing.

Do not assume the two files already agree. History contains bumps that changed only the C++ file, so the Go proxy version may already lag before your change. Read both current values first; if they disagree, report the drift to the maintainer rather than silently bumping from different bases.

Add a new top entry to `trunk/doc/CHANGELOG.md` under `## SRS 8.0 Changelog`, matching the existing format exactly:

```
* v8.0, YYYY-MM-DD, Merge [#PR](URL): <Prefix>: <one-line summary>. v8.0.<rev> (#PR)
```

Propose the summary to the user; don't invent one unilaterally.

## Oryx

Inspect `platform/version.go`, the newest entries under `DEVELOPER.md#changelog`, tags, and branch history before proposing a version. Do not assume the version constant and newest changelog entry are already synchronized.

When the maintainer approves an Oryx version update, change `platform/version.go` and add the smallest matching entry under the current series in `DEVELOPER.md`. Do not change `releases/version.go`; its legacy `latest`, `api`, and `stable` values are a separate compatibility service unless the PR explicitly changes that service.

## Both projects

Do not force a version bump for documentation, skill, issue-template, or maintenance-only work when the maintainer does not intend a release. Ask rather than infer.

Stop and let the user review and stage the version and changelog files. After an explicit commit request, follow `SKILL.md`'s repository-aware Git Workflow.
