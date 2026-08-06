---
name: internal-codemap-for-srs
description: Route SRS code tasks to the smallest relevant trusted codebase map and verification guidance. Use whenever support, development, debugging, review, or maintenance work requires locating, choosing, reading, modifying, testing, or verifying SRS code. Covers the first-generation C++ origin and edge media server, its State Threads dependency, the next-generation Go server, browser publishers and players for WHIP, WHEP, HTTP-FLV, and HLS, and the test, E2E, and benchmark structure. Use as the code-navigation dependency of srs-support and srs-develop; the parent skill remains responsible for the user-facing workflow and result.
---

# SRS Internal Code Map

Route code work to focused codebase maps. The parent skill owns the user-facing task; this skill owns code navigation and verification-file selection.

## Core Rules

- Use the current working directory as the project root. Do not search parent directories or discover alternate repository roots.
- Use the Reference Router before reading, searching, or modifying code, configuration, tests, or verification scripts.
- Treat only files and module directories listed by the selected reference as trusted navigation scope.
- Never grep the repository root or broad trees such as `trunk/src/`, `cmd/`, or `internal/`.
- When a selected reference lists a module directory rather than every file, list filenames only inside that module, choose the smallest relevant set, then read or search only those files.
- If no route covers the task, report that the code router does not cover it. Do not discover a new route ad hoc.
- Use `skills/internal-docs-for-srs/SKILL.md` for project documentation. Do not route documentation here.

## Path Resolution

- Resolve bundled paths beginning with `references/`, `scripts/`, `assets/`, or `agents/` relative to the directory containing this `SKILL.md`, not the current working directory.
- Resolve repository paths such as `trunk/`, `internal/`, `cmd/`, or `skills/` relative to the current working directory.
- Use the currently invoked skill directory. Do not search for alternate copies under tool-specific directories such as `.agents/`, `.kiro/`, or `.claude/`.
- Before reporting a routed file as missing, check its fully resolved path directly.

## Reference Router

| Code area | Use when | Load |
|---|---|---|
| C++ media server | The task concerns the first-generation origin or edge server, `trunk/src/`, `trunk/conf/`, protocols, media processing, or State Threads | `references/cpp-server.md` |
| Next-generation Go server | The task concerns the Go proxy, future Go origin or edge services, `cmd/`, or `internal/` | `references/go-server.md` |
| Browser publishers and players | The task concerns browser publishing or playback with WHIP, WHEP, HTTP-FLV, or HLS, including code under `trunk/research/players/` | `references/browser-clients.md` |
| Testing and verification | The task requires choosing or running unit, black-box, E2E, proxy, reproduction, or benchmark verification | `references/testing.md` |

For a comparison or migration across generations, load both server maps. Add the testing reference only when verification is required.

## Workflow

1. Classify the request as C++ media server, next-generation Go server, browser publishers and players, testing and verification, or an explicit combination.
2. If the server generation is unclear and choosing incorrectly could change the result, ask the user to clarify. Do not guess.
3. Resolve the selected path according to [Path Resolution](#path-resolution), then load the reference file or files.
4. Use their descriptions to identify the responsible module and the smallest relevant file set.
5. Read or search only those files. Add a second routed module only when evidence shows a dependency crosses the first module boundary.
6. Return control to the parent skill for support, development, debugging, review, or maintenance.

## Maintaining the Router

- Add, remove, or rename code modules and trusted files in exactly one server reference.
- Update the Reference Router when a reference is added, removed, renamed, or changes responsibility.
- Keep module descriptions concise and focused on ownership, boundaries, and navigation.
- Keep verification commands and test-suite responsibilities in `references/testing.md`; do not duplicate them in dependent skills.
