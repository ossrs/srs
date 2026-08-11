---
name: srs-develop
description: Develop, modify, debug, review, maintain, and explain the SRS codebase and Docker image toolchain. Use for planned changes to the next-generation Go server, SRS browser player, or ossrs/dev-docker images; bug maintenance; issue and pull-request triage; pull-request review; and Learn Code questions about how existing C++, Go, browser, or Docker build code works. Planned development is currently supported for the Go proxy server, SRS player, and Dev Docker; the C++ server is in maintenance mode, and planned Go origin and edge development is not yet supported. NOT for end-user support, usage questions, or configuration help — use the srs-support skill for those.
---

# SRS Development

## Core Principle

**Code and documents are the only truth.** Issue descriptions may be inaccurate. Pull requests may be misleading. Feature descriptions may be insufficient. Always ground your understanding in the actual source code and project documentation. Documents capture design intent, architecture rationale, and complex background that code alone cannot express — they are another form of code. When code and documents conflict, investigate rather than assume one is wrong.

## Skill Dependencies

- `skills/internal-docs-for-srs/SKILL.md` — Route and load project documentation. This skill remains responsible for the development workflow and final result.
- `skills/internal-codemap-for-srs/SKILL.md` — Route code navigation and verification to the relevant server map. This skill remains responsible for the development workflow and final result.

## Path Resolution

- Use the current working directory as the project root. Do not search parent directories or discover alternate repository roots.
- For the Dev Docker service only, use the configured `~/git/dev-docker` checkout through `git -C` while keeping the current working directory unchanged. If it does not exist, ask the user to clone `https://github.com/ossrs/dev-docker` into that exact path; do not clone it automatically or search for another checkout.
- Resolve bundled paths beginning with `references/`, `scripts/`, `assets/`, or `agents/` relative to the directory containing this `SKILL.md`, not the current working directory.
- Resolve repository paths such as `trunk/`, `internal/`, `cmd/`, or `skills/` relative to the current working directory.
- Use the currently invoked skill directory. Do not search for alternate copies under tool-specific directories such as `.agents/`, `.kiro/`, or `.claude/`.
- Before reporting a routed file as missing, check its fully resolved path directly.

## Git Workflow

Apply these rules whenever a task produces a commit:

- Never run `git add`; William stages the files he approves.
- Never run `git push`; William pushes the branch.
- Commit only when William explicitly asks.
- Before committing, run `git diff --cached`, understand the staged changes, and write an appropriate title and description.
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

- **Develop Code** — Use for any planned project change: adding or modifying functionality, refactoring code, changing Docker images, or updating project and skill documentation. This workflow routes development to the supported Go proxy, SRS player, or Dev Docker service and keeps unsupported server work out of scope. → [Develop Code workflow](references/develop-code.md)
- **Scan Issues** — Use when the maintainer wants a read-only scan of recently active open issues that may need attention. This workflow orders issues by meaningful activity and compares each one with its latest authorized Truth Record without changing GitHub. → [Scan Issues workflow](references/scan-issues.md)
- **Scan PRs** — Use for a read-only assessment of a specified pull request, or the single most recently updated open pull request when none is specified. This workflow examines the complete discussion, diff, reviews, and checks without posting a review or modifying the pull request. → [Scan PRs workflow](references/scan-prs.md)
- **Fix a Bug** — Use when an issue reports broken, unexpected, unsafe, or otherwise incorrect behavior and may require investigation or a maintenance fix. This workflow verifies the report, pauses for maintainer approval at Truth Record boundaries, performs only the approved update, and records the final result. → [Fix a Bug workflow](references/fix-a-bug.md)
- **Learn Code** — Use when the user wants an explanation of existing implementation, architecture, control flow, or behavior without project changes. This workflow routes to the smallest relevant documentation and code map, reconciles them, and answers with focused source references. → [Learn Code workflow](references/learn-code.md)
- **Review a PR** — Use for the maintainer's current-branch integration workflow after changes are already present locally. This workflow surveys the branch relative to `develop`, corrects stale navigation docs, then handles the version and changelog update after the pull request number is known. → [Review a PR workflow](references/review-a-pr.md)

**If the routed task is not yet supported**, stop and tell the user:
- What task type you routed to
- That this task type is not supported yet
- That support will be added in the future

Do NOT attempt unsupported tasks.
