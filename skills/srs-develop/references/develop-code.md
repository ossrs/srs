# Develop Code

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Develop Code**. Do not execute it directly.

**Scope:** This task covers any planned SRS or Oryx code or documentation change — adding features, modifying functionality, refactoring code, changing packaging, and updating project or skill documentation.

**Important:** The SRS player, Dev Docker, State Threads, and the website have separate workflows below.

**Version and changelog:** If the change bumps a version or adds a changelog entry, load `references/version-and-changelog.md` and apply it, whichever service you routed to.

**Service Router** — Determine which service or product the change targets. Route to exactly ONE service. Do not guess — if unclear, ask the user to clarify.

| Service | Route To | Status |
|---|---|---|
| **Proxy server** | → [Proxy Server](#proxy-server) | ✅ Supported |
| **SRS player** | → [SRS Player](#srs-player) | ✅ Supported |
| **Dev Docker** | → [Dev Docker](#dev-docker) | ✅ Supported |
| **Oryx** | → [Oryx](#oryx) | ✅ Supported |
| **C++ media server maintenance** | → [C++ Media Server Maintenance](#c-media-server-maintenance) | ✅ Supported |
| **State Threads** | → [State Threads](#state-threads) | ✅ Supported |
| **Website** | → [Website](#website) | 🔒 Private skill |
| **Origin server** | → [Origin Server](#origin-server) | ❌ Not yet supported |
| **Edge server** | → [Edge Server](#edge-server) | ❌ Not yet supported |

**If the routed service is not yet supported**, stop and tell the user:
- What service you routed to
- That this service is not supported yet

## Proxy Server

The proxy server is a complex, growing product — not a small app. It has many modules, and more will be added over time. You cannot load all the code into context at once. The key to working on it is **routing to the correct module first**.

### Step 1: Module Routing (MANDATORY)

1. Load `skills/internal-codemap-for-srs/SKILL.md`, then use its Reference Router to select the next-generation Go server code map.
2. Load `skills/internal-docs-for-srs/SKILL.md`, then use its Reference Router to select the relevant next-generation server documentation references.
3. Study the routed module and document descriptions. Understand what each covers and its boundaries.
4. Reason about which module(s) and which document(s) are relevant to the user's request. Consider:
   - Which module owns the functionality being changed?
   - Which modules might be affected as dependencies?
   - Which docs cover the design/architecture of this area?
   - Is this a new module or a change to an existing one?
5. **Present your reasoning to the user — both the module(s) and document(s) you identified — and ask for confirmation.** Even if you are confident, you MUST ask. Do not proceed without confirmation.
6. If you are unsure, stop and ask the user to clarify. Do not guess.

Only after the user confirms the routing do you proceed to Step 2.

### Step 2: Understand the Module

1. **Read the confirmed docs** (if any were identified) — understand the design intent, architecture rationale, and how the module is organized internally. This is the *why*.
2. **Based on doc understanding, identify the specific file(s)** within the module that are relevant to the feature. Not the whole module — only the files that matter.
3. **Read only those specific files.** Code gives you the implementation details: function signatures, patterns, conventions, edge cases. This is the *how*.
4. If no relevant docs exist, scan the module directory listing (filenames only) to locate the right files, then read them.

### Step 3: Implement and Verify

1. Implement the code change, adding test coverage under the test-first contract in `references/testable-code.md`.
2. If you changed or added a Go interface with a `//go:generate go tool counterfeiter ...` directive, regenerate fakes:
   ```
   make generate
   ```
3. Run the complete cross-component verification matrix in `references/integration-tests.md`. Run every command sequentially and record every result.

## SRS Player

### Step 1: Route and Understand

1. Load `skills/internal-codemap-for-srs/SKILL.md`, then use its Reference Router to select the browser publishers and players code map.
2. Load `skills/internal-docs-for-srs/SKILL.md`, then use its Reference Router to select the smallest relevant documentation set when the router covers the change.
3. Use the routed descriptions to identify and read only the player files and documentation relevant to the requested change.

### Step 2: Implement and Verify

1. Implement the player change.
2. Verify browser player URL generation independently with Node.js; a running SRS server is not required:
   ```bash
   node scripts/browser-page-url-test.js
   ```
3. If the change also requires server, protocol, E2E, or benchmark verification, use the Tests and Verification section of `skills/internal-codemap-for-srs/references/cpp-server.md` to select and run the relevant tests.

## Dev Docker

Dev Docker is maintained in the separate `ossrs/dev-docker` repository. Its long-lived branches independently define base, cache, cross-build, compatibility, and specialized images, so route to the owning branch before reading or changing files.

### Step 1: Route the Branch and Files (MANDATORY)

1. Load `skills/internal-codemap-for-srs/SKILL.md`, route to the SRS Docker build images map, and read `skills/internal-codemap-for-srs/references/dev-docker.md`.
2. Check the project-root-relative `dev-docker/` path directly. It may be a directory or a symlink to the user's preferred checkout. If it is unavailable, stop and ask the user to make the `https://github.com/ossrs/dev-docker` checkout available there.
3. Keep the SRS repository as the current working directory. Use `git -C dev-docker/ ...` for every Dev Docker Git operation and do not resolve or replace the relative path.
4. Check the status, current branch, commit, and remote branches of both repositories. Do not switch a dirty Dev Docker worktree or overwrite unrelated changes.
5. Identify the single owning branch, smallest relevant Dockerfile or workflow set, produced image tags and platforms, parent layers, and downstream SRS or cache images affected by the change.
6. Present that routing and dependency impact to the user and ask for confirmation. Do not edit Dev Docker before confirmation.

### Step 2: Understand the Image Path

1. Read only the confirmed branch files. When the branch is not checked out, inspect it with `git show origin/<branch>:<path>` rather than switching merely to read it.
2. Trace the image from its parent `FROM` layer through dependency installation, named tool binaries, aggregation or default symlinks, and `.github/workflows/release.yml` publication jobs.
3. When the tool is packaged into an SRS release image, also inspect the SRS root `Dockerfile` and confirm the exact source binary copied into `objs/ffmpeg/bin/` or another runtime location.
4. Record the vendored archive, version, build options, target architectures, image tags, cache consumers, and compatibility constraints relevant to the requested change.

### Step 3: Implement the Confirmed Change

1. Switch to or create the confirmed Dev Docker branch only after verifying its worktree is clean. Keep the shell working directory unchanged and use `git -C`.
2. Make the smallest dependency-layer, archive, aggregation, workflow, cache, or specialized-image change required. Do not update compatibility branches unless they are explicitly in scope.
3. When changing a dependency version, keep the referenced vendored archive, Dockerfile version, named binaries, default selection, and workflow layer graph consistent.
4. Update downstream cache branches or the SRS root `Dockerfile` only when the confirmed dependency path requires it.

### Step 4: Verify

1. Run `git diff --check` and inspect the complete diffs in every modified repository.
2. Follow `skills/internal-codemap-for-srs/references/dev-docker.md` to build the smallest changed image layer, then the aggregate and final consumer images required by the dependency graph.
3. Verify the actual packaged binary and version inside the final image, not only the intermediate build layer.
4. Run an issue-specific media or protocol reproduction when the dependency change fixes behavior; a successful image build and version command are not sufficient.
5. Verify every affected architecture when the change can vary across amd64, arm64, or armv7. If required Docker, Buildx, registry, or architecture verification is unavailable, report the exact unverified scope rather than claiming success.
6. Do not push images or Git branches. Stop for user review and staging in each modified repository.

## Oryx

Oryx is maintained in the separate `ossrs/oryx` repository. It combines a Go platform backend, React dashboard, SRS, Redis, NGINX, FFmpeg workers, Docker packaging, installers, release automation, and black-box tests. Route to the owning feature before reading code.

For maintainer macOS development where Oryx should be built, run, debugged, and demonstrated locally without running Oryx in Docker, use [Oryx Local Development on macOS](oryx-local-development.md). This local workflow runs Redis, local SRS, the Go backend, and the React dashboard as separate processes.

### Step 1: Route the Product Area (MANDATORY)

1. Check the project-root-relative `oryx/` path directly. It may be a directory or a symlink to the user's preferred checkout. If it is unavailable, stop and ask the user to make the `https://github.com/ossrs/oryx` checkout available there.
2. Keep the SRS repository as the current working directory. Use `git -C oryx/ ...` for Oryx Git operations and do not resolve or replace the relative path.
3. Inspect the status, branch, commit, and remotes of both repositories. Do not overwrite unrelated work or switch a dirty Oryx worktree.
4. Load `skills/internal-codemap-for-srs/SKILL.md`, route to `references/oryx.md`, and identify the smallest responsible backend, UI, runtime, packaging, installer, release, and test slice.
5. Load `skills/internal-docs-for-srs/SKILL.md` and select the smallest relevant Oryx documentation set. Prefer version-matched repository documentation over dated scenario blogs.
6. Present the selected Oryx files, documents, product boundaries, and proposed verification to the user. Ask for confirmation before editing.

### Step 2: Understand the Oryx Path

1. Read the confirmed documents for intended behavior and constraints.
2. Read only the confirmed source files. Trace the narrowest path from dashboard or OpenAPI input through the Go handler and worker to Redis, SRS, FFmpeg, files, or external services as relevant.
3. When the task crosses into standalone SRS, load only the responsible SRS code map. Do not treat Oryx-generated SRS configuration as standalone SRS ownership.
4. Identify persistent `/data` state, secrets, external APIs, long-running workers, and generated configuration affected by the change.

### Step 3: Implement the Confirmed Change

1. Make the smallest change in `oryx/` and add focused regression coverage, applying the test-first contract in `references/testable-code.md`.
2. Keep backend JSON fields, OpenAPI behavior, React consumers, locale strings, and feature status views consistent when the change crosses those surfaces.
3. Do not edit vendored dependencies, compiled UI output, generated binaries, symlinked runtime directories, or `platform/containers/data/` state unless the confirmed task explicitly owns them.
4. Never expose administrator passwords, publish secrets, Bearer tokens, OpenAI keys, destination stream keys, or cloud credentials in source, tests, logs, or fixtures.

### Step 4: Verify

1. Run `git -C oryx/ diff --check` and inspect the complete Oryx diff.
2. Follow `skills/internal-codemap-for-srs/references/oryx.md` to run the smallest applicable Go, React, shell, Python, release-service, image, or integration verification.
3. Run black-box tests only against an explicitly disposable Oryx instance. Start with one selected `-run` case before widening scope.
4. Do not request real certificates, call OpenAI or cloud services, publish images, create releases, modify DNS, or use production stream keys unless the user explicitly authorizes that external effect.
5. If full Docker, media, browser, installer, or external-service verification is unavailable, report the exact unverified scope. Do not claim success from compilation alone.

## C++ Media Server Maintenance

Accept new features, bug fixes, security fixes, compatibility fixes, tests, and documentation corrections.

### Step 1: Route and Reproduce

1. Load `skills/internal-codemap-for-srs/SKILL.md`, route to `references/cpp-server.md`, and select the smallest responsible module and file set.
2. Load `skills/internal-docs-for-srs/SKILL.md` and select only the documentation relevant to the affected behavior.
3. When verification is required, load the Tests and Verification section of `skills/internal-codemap-for-srs/references/cpp-server.md` and choose the smallest unit, black-box, E2E, or benchmark surface that can reproduce the report.
4. Reproduce the problem on the relevant branch and version before modifying code. Record the exact command, configuration, input, and observed result.

### Step 2: Implement the Maintenance Fix

1. Identify the verified root cause and implement the smallest compatible fix.
2. Preserve C++98 compatibility and the existing coroutine, resource-ownership, protocol, configuration, and error-handling conventions.
3. Add focused regression coverage following `references/testable-code.md`: write the test first, run it, and confirm it fails before implementing the fix. When the code under test is not mockable, refactor it for testability first; that refactor is behavior-preserving and is accepted in maintenance mode.
4. Update user documentation only when the behavior or maintenance result changes what operators need to know.

### Step 3: Verify

1. Run the focused regression first, then the complete applicable verification selected from the Tests and Verification section of `skills/internal-codemap-for-srs/references/cpp-server.md`.
2. Run every command in `references/integration-tests.md`. These scripts are mandatory cross-component verification for C++ media-server changes, despite their `proxy-*` names, and supplement rather than replace C++ unit, black-box, protocol E2E, and benchmark tests.
3. Build the C++ server with the affected feature enabled and run `git diff --check`.
4. For external-SIP GB28181 session cleanup, run the focused regression for the affected terminal event. Use `scripts/gb28181-tcp-disconnect-test.sh` when a bound RTP/PS-over-TCP publisher disconnects, and `scripts/gb28181-api-timeout-test.sh` when an API-created session never receives a TCP connection. Each script builds and starts a disposable SRS with `--gb28181=on` and no embedded SIP server, then requires the same ID and SSRC to become publishable again.
5. Run sanitizer verification when the fix changes coroutine interruption, sockets, resource ownership, shared pointers, or object destruction.
6. Report every test result and any unverified platform or configuration scope. Do not push or stage files.

## State Threads

State Threads is maintained in the separate `ossrs/state-threads` project at `state-threads/`, the only ST code to read or change. `trunk/3rdparty/st-srs/` is a vendored mirror for the SRS build; never edit it directly. Make every ST change in `state-threads/`, then overwrite the mirror with it. When a C++ media-server task traces its root cause into ST, route the ST part here.

### Step 1: Route the Change (MANDATORY)

1. Load `skills/internal-codemap-for-srs/SKILL.md`, route to the State Threads map, and read `skills/internal-codemap-for-srs/references/state-threads.md`.
2. Load `skills/internal-docs-for-srs/SKILL.md` and select only the State Threads documents relevant to the change.
3. Check the project-root-relative `state-threads/` path directly. If it is unavailable, stop and ask the user to make the `https://github.com/ossrs/state-threads` checkout available there.
4. Check the status, branch, and commit of both repositories.
5. Identify the responsible source files, affected platforms and CPU architectures, build macros, and the SRS wrappers that consume the changed API.
6. Present that routing and impact to the user and ask for confirmation. Do not edit before confirmation.

### Step 2: Implement in State Threads

1. Add or update a test under `state-threads/utest/` first, following `references/testable-code.md`, and confirm it fails before the fix.
2. Make the smallest change in `state-threads/`. Keep plain C that builds with the compilers of every supported platform, the public API in `public.h`, and the per-platform assembly in `md_*.S` consistent with `md.h` and the `jmpbuf` layout.
3. Do not change behavior on a platform you cannot build and test without saying so to the user.

### Step 3: Verify State Threads

1. Run `git -C state-threads/ diff --check` and inspect the complete ST diff.
2. Run every test on macOS and on Linux as the map describes:
   - Unit tests: `utest/`.
   - Integration tests: `tools/`, after the unit tests.
   - ASAN or Valgrind build: when the change touches stacks, context switching, `st_destroy`, or thread lifetime.
   - Each platform the map detects, never assumed, with `scripts/st-test.sh`: macOS locally, then Linux in Docker and, for a committed change, Windows in parallel.
   - On a Windows host: the Windows unit and integration tests locally, and the Linux ones in WSL on the same checkout.
3. Report the results per OS and CPU, as the test platforms in `references/testable-code.md` describe, and every CPU and OS combination touched by the change that was not built and tested.

### Step 4: Sync and Verify SRS

1. Overwrite the mirror `trunk/3rdparty/st-srs/` with `state-threads/` by running `./scripts/copy-from-st.sh` from the project root.
2. Rebuild SRS so `trunk/auto/depends.sh` rebuilds ST instead of reusing a cached `libst.a`, as the map describes.
3. Run the SRS unit tests, the SRS wrapper tests the change affects, and every command in `references/integration-tests.md`.
4. Follow `references/version-and-changelog.md` for the ST version and changelog, and for the SRS version when the sync bumps it.
5. Stop for user review and staging in each modified repository. Do not push.

### Step 5: Verify State Threads on Windows

After the ST change is committed, by the user or by a task that may commit, and before the pull request:

1. Run `bash skills/srs-develop/scripts/st-windows-test.sh` from the project root, as the State Threads map describes. It sets up a mirror of the checkout on a remote Windows host and tests native Windows x64 there.
2. It runs only when `SRS_TEST_WINDOWS_HOST` is set. When the commit is not on the branch's upstream yet, such as a personal fork, it pushes it there only to sync the branch to the test host, never to `origin`; this is the one push the workflow allows. When it skips or setup fails, report Windows as not tested with its reason.
3. Add its `RESULT` lines to the per-OS report. A failure goes back to Step 2.

## Website

The website is maintained in the separate `website/` repository by a private AI skill, because its development manages cloud resources that cannot be described in this public skill. Do not develop the website with this workflow; stop and tell the user to use the private website skill.

Documentation content is not website development. Update the English documents bundled in `skills/internal-docs-for-srs/` with this workflow; translation, framework, and release stay with the private website skill.

## Origin Server

**Not yet supported.** This refers to the next-generation origin server workflow.

## Edge Server

**Not yet supported.** Will be added in a future update.
