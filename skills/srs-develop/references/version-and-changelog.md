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

**Bump all five together. Bumping only `platform/version.go` is a defect.**

| File | Field | Value |
|---|---|---|
| `platform/version.go` | `const version` | `v7.15.32` — with the `v` |
| `scripts/setup-aapanel/info.json` | `"versions"` | `7.15.32` |
| `scripts/setup-bt/info.json` | `"versions"` | `7.15.32` |
| `scripts/setup-droplet/srs.json` | `"application_version"` | `7.15.32` |
| `DEVELOPER.md` | changelog entry | see below |

Read all five current values first; they drift. Do not change `releases/version.go`; its legacy `latest`, `api`, and `stable` values are a separate compatibility service.

Append the entry as the **last** line of the current series under `## Changelog`. Oryx is newest-last within a series and newest-series-first — the opposite of the SRS convention:

```
* v7.15:
    * Docker: Upgrade the build and runtime toolchain and use SRS 7. v7.15.25
    * <Prefix>: <one-line summary>. v7.15.32
```

Propose the summary to the user; don't invent one unilaterally.

**Never publish a release.** Do not run `auto/pub.sh`, create or push a tag, or trigger the release workflow. Once the bump is committed and pushed, report the command and stop:

```bash
cd oryx && ./auto/pub.sh --target v7.15.32
```

`--target` is required: the script parses flags only, and without it derives the last tag and bumps the patch by one. It refuses to tag until the four code locations match the target, the worktree is clean, and the branch is in sync with `origin`.

## Releasing a New Version

A version bump is not a release. Bumping happens on every merged change; releasing tags one chosen revision and is a separate, deliberate act. Never release — prepare the bump, then report the command and stop.

- **SRS** — Push a `v8*` tag to trigger `.github/workflows/release.yml`. Development happens on `develop`; each released series has a long-lived `<MAJOR>.0release` branch.
- **Oryx** — `cd oryx && ./auto/pub.sh --target vX.Y.Z`. The branch decides the release type: a tag from `main` publishes a prerelease, a tag from `release/X.Y` publishes the stable/latest release. Not every revision is tagged; after publishing, link that changelog entry to its release page.
- **Proxy** — No release process exists yet. Its version is bumped alongside SRS but never released separately. Do not invent one.

## Both projects

Do not force a version bump for documentation, skill, issue-template, or maintenance-only work when the maintainer does not intend a release. Ask rather than infer.

Stop and let the user review and stage the version and changelog files. After an explicit commit request, follow `SKILL.md`'s repository-aware Git Workflow.
