---
name: srs-develop
description: Develop, modify, debug, review, maintain, and explain the SRS and Oryx codebases and the SRS Docker image toolchain. Use for planned changes to the next-generation SRS Go proxy, SRS browser player, ossrs/dev-docker images, or Oryx Go backend, React dashboard, integrated runtime, packaging, installers, releases, and tests; bug maintenance; issue and pull-request triage; pull-request review; and Learn Code questions. The C++ SRS server is in maintenance mode, and planned Go origin and edge development is not yet supported. NOT for end-user support, usage questions, or configuration help — use srs-support for those.
---

# SRS and Oryx Development

## Core Principle

**Code and documents are the only truth.** Issue descriptions may be inaccurate. Pull requests may be misleading. Feature descriptions may be insufficient. Always ground your understanding in the actual source code and project documentation. Documents capture design intent, architecture rationale, and complex background that code alone cannot express — they are another form of code. When code and documents conflict, investigate rather than assume one is wrong.

## Skill Dependencies

- `skills/internal-docs-for-srs/SKILL.md` — Route and load project documentation. This skill remains responsible for the development workflow and final result.
- `skills/internal-codemap-for-srs/SKILL.md` — Route code navigation and verification to the relevant server map. This skill remains responsible for the development workflow and final result.

## Cross-Component Verification

For every standalone SRS runtime code change in either the Go proxy or C++ media server, run the complete bundled suite in `references/integration-tests.md` in addition to module-specific unit, black-box, protocol E2E, sanitizer, or benchmark verification. Apply this requirement during development, bug fixing, and pull-request review; do not treat the `proxy-*` script names as limiting the suite to proxy changes.

## Path Resolution

- Use the current working directory as the project root. Do not search parent directories or discover alternate repository roots.
- For Oryx, use the project-root-relative `oryx/` path through `git -C` while keeping the current working directory unchanged. The path may be a directory or a symlink to the user's preferred checkout. If it is unavailable, ask the user to make the Oryx checkout available there; do not create it automatically, resolve the symlink, or search for another checkout.
- For Dev Docker, use the project-root-relative `dev-docker/` path through `git -C` while keeping the current working directory unchanged. The path may be a directory or a symlink to the user's preferred checkout. If it is unavailable, ask the user to make the Dev Docker checkout available there; do not create it automatically, resolve the symlink, or search for another checkout.
- Resolve bundled paths beginning with `references/`, `scripts/`, `assets/`, or `agents/` relative to the directory containing this `SKILL.md`, not the current working directory.
- Resolve repository paths such as `trunk/`, `internal/`, `cmd/`, or `skills/` relative to the current working directory.
- Use the currently invoked skill directory. Do not search for alternate copies under tool-specific directories such as `.agents/`, `.kiro/`, or `.claude/`.
- Before reporting a routed file as missing, check its fully resolved path directly.

## Git Workflow

Apply these rules whenever a task produces a commit:

- Identify the owning repository before inspecting or committing staged changes. Use the current repository for SRS and skills, `git -C oryx/` for Oryx, and `git -C dev-docker/` for Dev Docker.
- Never run `git add`; the user stages the files they approve.
- Never run `git push`; the user pushes the branch.
- Commit only when the user explicitly asks.
- Before committing, run the owning repository's staged diff, understand every staged change, and write an appropriate title and description. Do not include staged changes from another repository in the same commit.
- Prefix the commit title with the tool that made the changes: `OpenClaw:`, `Claude:`, or `Codex:`.
- If Claude made changes, use this exact commit message format:
  ```
  Commit title.

  Commit description.

  ---------

  Co-authored-by: Claude Fable 5 <noreply@anthropic.com>
  ```
- If Codex made changes, use this exact commit message format:
  ```
  Commit title.

  Commit description.

  ---------

  Co-authored-by: chatgpt-codex-connector[bot] <199175422+chatgpt-codex-connector[bot]@users.noreply.github.com>
  ```

## Task Router

⚠️ **MANDATORY — Always execute this step first.** Never skip the Task Router. Never jump directly to a task. Every request must be routed through this router before any work begins.

Route the user's request to exactly ONE task type. Follow that task only. Do not combine tasks.

Choose exactly one supported task:

- **Develop Code** — Use for any planned SRS, Oryx, Dev Docker, project-documentation, or skill change. This workflow routes development to the supported Go proxy, SRS player, Dev Docker, or Oryx service and keeps unsupported server work out of scope. → [Develop Code workflow](references/develop-code.md)
- **Scan Issues** — Use for a read-only scan of recently active open SRS or Oryx issues that may need attention. Select exactly one repository and compare issues with that project's latest authorized Truth Records without changing GitHub. → [Scan Issues workflow](references/scan-issues.md)
- **Scan PRs** — Use for a read-only assessment of a specified SRS or Oryx pull request, or the single most recently updated open pull request in one selected repository. Examine the complete discussion, diff, reviews, and checks without modifying the pull request. → [Scan PRs workflow](references/scan-prs.md)
- **Fix a Bug** — Use when an SRS or Oryx issue reports broken, unexpected, unsafe, or otherwise incorrect behavior and may require investigation or a maintenance fix. Verify the report, pause for maintainer approval at Truth Record boundaries, perform only the approved update, and record the final result in the selected project's record. → [Fix a Bug workflow](references/fix-a-bug.md)
- **Learn Code** — Use when the user wants an explanation of existing SRS or Oryx implementation, architecture, control flow, or behavior without project changes. Route to the smallest relevant documentation and code map, reconcile them, and answer with focused source references. → [Learn Code workflow](references/learn-code.md)
- **Review a PR** — Use for the maintainer's integration workflow after SRS or Oryx changes are already present locally. Select one repository, survey the branch relative to its actual base, correct stale navigation docs, then apply the selected project's version and changelog rules when required. → [Review a PR workflow](references/review-a-pr.md)

**If the routed task is not yet supported**, stop and tell the user:
- What task type you routed to
- That this task type is not supported yet
- That support will be added in the future

Do NOT attempt unsupported tasks.

For every task, identify SRS or Oryx before loading a workflow. If the product is ambiguous and the choice changes the repository, issue tracker, code map, or verification, ask the user to clarify. Do not merge SRS and Oryx work unless evidence shows one task crosses the product boundary.
