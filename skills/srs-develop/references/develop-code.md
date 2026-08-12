# Develop Code

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Develop Code**. Do not execute it directly.

**Scope:** This task covers any planned SRS or Oryx code or documentation change — adding features, modifying functionality, refactoring code, changing packaging, and updating project or skill documentation.

**Important:** The C++ media server (origin + edge) is in **maintenance mode** — only bug fixes are accepted, no new features. New server features belong in the next-generation Go server. The SRS player and Dev Docker have separate supported workflows below. You may reference the C++ server's code to understand how things were done before, but do not add features to it.

**Service Router** — Determine which service or product the change targets. Route to exactly ONE service. Do not guess — if unclear, ask the user to clarify.

| Service | Route To | Status |
|---|---|---|
| **Proxy server** | → [Proxy Server](#proxy-server) | ✅ Supported |
| **SRS player** | → [SRS Player](#srs-player) | ✅ Supported |
| **Dev Docker** | → [Dev Docker](#dev-docker) | ✅ Supported |
| **Oryx** | → [Oryx](#oryx) | ✅ Supported |
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

1. Implement the code change.
2. If you changed or added a Go interface with a `//go:generate go tool counterfeiter ...` directive, regenerate fakes:
   ```
   make generate
   ```
3. Run the unit test first, then every E2E test below in order. The E2E scripts bind fixed ports, so run them sequentially. Do not stop after an early success or failure; record every result, fix failures, and repeat until all required tests pass.

   1. Go proxy unit tests with coverage:
      ```bash
      bash scripts/proxy-utest.sh --coverage
      ```
   2. Single-origin RTMP proxy:
      ```bash
      bash scripts/proxy-e2e-test.sh
      ```
   3. Multi-origin memory load-balancer routing:
      ```bash
      bash scripts/proxy-e2e-cluster-test.sh
      ```
   4. Proxy, SRS edge, and SRS origin three-tier topology with a late-joining player:
      ```bash
      bash scripts/proxy-e2e-edge-test.sh
      ```
   5. Redis multi-proxy routing:
      ```bash
      bash scripts/proxy-e2e-redis-test.sh
      ```
   6. RTMP publish with RTMP, HTTP-FLV, and HLS playback verification; WHEP remains a placeholder:
      ```bash
      bash scripts/proxy-e2e-transmux-test.sh
      ```
   7. SRT publish with SRT, RTMP, HTTP-FLV, and HLS playback verification; WHEP remains a placeholder:
      ```bash
      bash scripts/proxy-e2e-srt-test.sh
      ```
   8. WHIP publish with RTMP, HTTP-FLV, and HLS playback verification; WHEP remains a placeholder:
      ```bash
      bash scripts/proxy-e2e-whip-test.sh
      ```

   The SRT test requires an FFmpeg build with libsrt. The WHIP test requires the `whip` muxer and OpenSSL. Both scripts automatically run `scripts/setup-ffmpeg-with-whip.sh` on macOS when no suitable FFmpeg is available.

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
3. If the change also requires server, protocol, E2E, or benchmark verification, use `skills/internal-codemap-for-srs/references/testing.md` to select and run the relevant tests.

## Dev Docker

Dev Docker is maintained in the separate `ossrs/dev-docker` repository. Its long-lived branches independently define base, cache, cross-build, compatibility, and specialized images, so route to the owning branch before reading or changing files.

### Step 1: Route the Branch and Files (MANDATORY)

1. Load `skills/internal-codemap-for-srs/SKILL.md`, route to the SRS Docker build images map, and read `skills/internal-codemap-for-srs/references/dev-docker.md`.
2. Check `~/git/dev-docker` directly. If it does not exist, stop and ask the user to clone `https://github.com/ossrs/dev-docker` into `~/git/dev-docker`.
3. Keep the SRS repository as the current working directory. Use `git -C ~/git/dev-docker ...` for every Dev Docker Git operation.
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

### Step 1: Route the Product Area (MANDATORY)

1. Check `~/git/oryx` directly. If it does not exist, stop and ask the user to clone `https://github.com/ossrs/oryx` into `~/git/oryx`.
2. Keep the SRS repository as the current working directory. Use `git -C ~/git/oryx ...` for Oryx Git operations and do not expand the configured path.
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

1. Make the smallest change in `~/git/oryx` and add focused regression coverage.
2. Keep backend JSON fields, OpenAPI behavior, React consumers, locale strings, and feature status views consistent when the change crosses those surfaces.
3. Do not edit vendored dependencies, compiled UI output, generated binaries, symlinked runtime directories, or `platform/containers/data/` state unless the confirmed task explicitly owns them.
4. Never expose administrator passwords, publish secrets, Bearer tokens, OpenAI keys, destination stream keys, or cloud credentials in source, tests, logs, or fixtures.

### Step 4: Verify

1. Run `git -C ~/git/oryx diff --check` and inspect the complete Oryx diff.
2. Follow `skills/internal-codemap-for-srs/references/oryx.md` to run the smallest applicable Go, React, shell, Python, release-service, image, or integration verification.
3. Run black-box tests only against an explicitly disposable Oryx instance. Start with one selected `-run` case before widening scope.
4. Do not request real certificates, call OpenAI or cloud services, publish images, create releases, modify DNS, or use production stream keys unless the user explicitly authorizes that external effect.
5. If full Docker, media, browser, installer, or external-service verification is unavailable, report the exact unverified scope. Do not claim success from compilation alone.

## Origin Server

**Not yet supported.** This refers to the next-generation Go origin server workflow. The first-generation C++ origin server still exists, but it is in maintenance mode and only bug fixes are accepted there.

## Edge Server

**Not yet supported.** Will be added in a future update.
