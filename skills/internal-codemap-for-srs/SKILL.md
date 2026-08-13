---
name: internal-codemap-for-srs
description: Route SRS and Oryx code tasks to the smallest relevant trusted codebase map and verification guidance. Use whenever support, development, debugging, review, or maintenance work requires locating, choosing, reading, modifying, testing, or verifying SRS or Oryx code. Covers the first-generation C++ origin and edge media server, its State Threads dependency, the next-generation Go server, browser publishers and players, the SRS Docker image toolchain, SRS testing, and the Oryx Go backend, React dashboard, integrated runtime, packaging, installers, releases, and tests. Use as the code-navigation dependency of parent support and development skills; the parent skill remains responsible for the user-facing workflow and result.
---

# SRS and Oryx Internal Code Map

Route SRS and Oryx code work to focused codebase maps. The parent skill owns the user-facing task; this skill owns code navigation and verification-file selection.

## Core Rules

- Use the current working directory as the project root. Do not search parent directories or discover alternate repository roots.
- The only external-repository exceptions are Dev Docker through the project-root-relative `dev-docker/` path and Oryx through the project-root-relative `oryx/` path. Keep the current working directory unchanged, do not resolve either symlink, and do not search for alternate checkouts.
- Use the Reference Router before reading, searching, or modifying code, configuration, tests, or verification scripts.
- Treat only files and module directories listed by the selected reference as trusted navigation scope.
- Never grep a repository root or broad trees such as `trunk/src/`, `cmd/`, `internal/`, `oryx/platform/`, or `oryx/ui/`.
- When a selected reference lists a module directory rather than every file, list filenames only inside that module, choose the smallest relevant set, then read or search only those files.
- If no route covers the task, report that the code router does not cover it. Do not discover a new route ad hoc.
- Use `skills/internal-docs-for-srs/SKILL.md` for project documentation. Do not route documentation here.

## Path Resolution

- Resolve bundled paths beginning with `references/`, `scripts/`, `assets/`, or `agents/` relative to the directory containing this `SKILL.md`, not the current working directory.
- Resolve repository paths such as `trunk/`, `internal/`, `cmd/`, or `skills/` relative to the current working directory.
- Resolve files selected by `references/dev-docker.md` through the project-root-relative `dev-docker/` path while keeping the current working directory unchanged. The path may be a directory or a symlink to the user's preferred checkout. If it is unavailable, ask the user to make the `https://github.com/ossrs/dev-docker` checkout available there; do not create it automatically.
- Resolve files selected by `references/oryx.md` through the project-root-relative `oryx/` path while keeping the current working directory unchanged. The path may be a directory or a symlink to the user's preferred checkout. If it is unavailable, ask the user to make the `https://github.com/ossrs/oryx` checkout available there; do not create it automatically.
- Use the currently invoked skill directory. Do not search for alternate copies under tool-specific directories such as `.agents/`, `.kiro/`, or `.claude/`.
- Before reporting a routed file as missing, check its fully resolved path directly.

## Reference Router

| Code area | Use when | Load |
|---|---|---|
| C++ media server | The task concerns the first-generation origin or edge server, `trunk/src/`, `trunk/conf/`, protocols, media processing, or State Threads | `references/cpp-server.md` |
| Next-generation Go server | The task concerns the Go proxy, future Go origin or edge services, `cmd/`, or `internal/` | `references/go-server.md` |
| Browser publishers and players | The task concerns browser publishing or playback with WHIP, WHEP, HTTP-FLV, or HLS, including code under `trunk/research/players/` | `references/browser-clients.md` |
| SRS Docker build images | The task concerns `ossrs/dev-docker`, Docker dependency or cache images, packaged FFmpeg and other build tools, image branches, or how the SRS release image receives those tools | `references/dev-docker.md` |
| Testing and verification | The task requires choosing or running C++ unit, black-box, E2E, reproduction, or benchmark verification | `references/testing.md` |
| Oryx integrated video solution | The task concerns `ossrs/oryx`, `oryx/`, its Go platform, React dashboard, SRS/Redis runtime integration, Docker image, installers, release service, or integration tests | `references/oryx.md` |

For a comparison or migration across SRS generations, load both server maps. Add the SRS testing reference only when SRS verification is required. For work crossing standalone SRS and Oryx, load the Oryx map and only the smallest responsible SRS map.

## Workflow

1. Classify the request as C++ media server, next-generation Go server, browser publishers and players, SRS Docker build images, SRS testing and verification, Oryx integrated video solution, or an explicit combination.
2. If the product or server generation is unclear and choosing incorrectly could change the result, ask the user to clarify. Do not guess whether the task targets standalone SRS or Oryx.
3. Resolve the selected path according to [Path Resolution](#path-resolution), then load the reference file or files.
4. Use their descriptions to identify the responsible module and the smallest relevant file set.
5. Read or search only those files. Add a second routed module only when evidence shows a dependency crosses the first module boundary.
6. Return control to the parent skill for support, development, debugging, review, or maintenance.

## Maintaining the Router

- Add, remove, or rename code modules and trusted files in exactly one server reference.
- Update the Reference Router when a reference is added, removed, renamed, or changes responsibility.
- Keep module descriptions concise and focused on ownership, boundaries, and navigation.
- Keep repository-native C++ server, protocol, E2E, and benchmark verification in `references/testing.md`. Keep verification scripts bundled with a parent skill in that owning skill; do not duplicate them here.
- Keep Oryx source navigation and Oryx-native verification in `references/oryx.md`; do not mix it into the standalone SRS maps.
