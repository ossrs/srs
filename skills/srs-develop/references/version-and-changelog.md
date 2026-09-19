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

A version bump is not a release. Bumping happens on every merged change; releasing tags one chosen revision and is a separate, deliberate act. Never release — prepare it locally, report the commands for the user to run, and stop.

- **SRS** — Development happens on `develop`; each released series has a long-lived `<MAJOR>.0release` branch. Pushing a `vX.0-<stage>` tag triggers that branch's `.github/workflows/release.yml`, which creates a draft GitHub release and Docker images; the user publishes the draft. To prepare a release on `<MAJOR>.0release`:
  1. Pick the tag: stages go `d` (dev) → `a` (alpha) → `b` (beta) → `r` (release), e.g. `v7.0-a0`. No version bump.
  2. Count lines: `git ls-files trunk/src | grep -E '\.(h|hpp|c|cpp|cc|S)$' | xargs cat | wc -l`.
  3. Add the top line under `## Releases` in `README.md`:
     `* 2026-09-18, [Release v7.0-a0](https://github.com/ossrs/srs/releases/tag/v7.0-a0), v7.0-a0, 7.0 alpha0, v7.0.162, 314832 lines.`
  4. Add the top line under `## SRS 7.0 Changelog` in `trunk/doc/CHANGELOG.md`:
     `* <strong>v7.0, 2026-09-18, [7.0 alpha0(7.0.162)](https://github.com/ossrs/srs/releases/tag/v7.0-a0) released. 314832 lines.</strong>`
  5. Commit as `Release v7.0-a0, 7.0 alpha0, v7.0.162, 314832 lines.`, then add the same `README.md` line to local `develop`.
  6. Stop. Report these commands for the user to run; never run them yourself: `git push origin 7.0release`, then `git tag v7.0-a0 && git push origin v7.0-a0`.
  7. After the user pushes the tag, wait for the workflow to create the release, then draft a `## Changelog` section for its description and ask the user to paste it at `https://github.com/ossrs/srs/releases/edit/v7.0-a0`; updating it manually is expected. Follow the previous release of the series (`gh release view v7.0-d0 -R ossrs/srs`) and put the section between the commit subject and `## Resource`:
     - One sentence naming the release and its baseline, e.g. `SRS 7.0-a0 is the first alpha release of SRS 7. Compared with v7.0-d0, ...`
     - One bullet per `CHANGELOG.md` entry since the previous tag (`git diff v7.0-d0 v7.0-a0 -- trunk/doc/CHANGELOG.md`), each linking its PR. For a large release, summarize the major changes and add `## Important Changes` for breaking changes.
     - `For all N commits, see the [SRS 7 changelog](https://github.com/ossrs/srs/blob/v7.0-a0/trunk/doc/CHANGELOG.md#srs-70-changelog).`
- **Oryx** — `cd oryx && ./auto/pub.sh --target vX.Y.Z`. The branch decides the release type: a tag from `main` publishes a prerelease, a tag from `release/X.Y` publishes the stable/latest release. Not every revision is tagged; after publishing, link that changelog entry to its release page.
- **Proxy** — No release process exists yet. Its version is bumped alongside SRS but never released separately. Do not invent one.

## Both projects

Do not force a version bump for documentation, skill, issue-template, or maintenance-only work when the maintainer does not intend a release. Ask rather than infer.

Stop and let the user review and stage the version and changelog files. After an explicit commit request, follow `SKILL.md`'s repository-aware Git Workflow.
