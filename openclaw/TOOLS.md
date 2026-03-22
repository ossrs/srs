# TOOLS.md - Local Notes

Skills define _how_ tools work. This file is for _your_ specifics — the stuff that's unique to your setup.

## What Goes Here

Things like:

- Camera names and locations
- SSH hosts and aliases
- Preferred voices for TTS
- Speaker/room names
- Device nicknames
- Anything environment-specific

## Examples

```markdown
### Cameras

- living-room → Main area, 180° wide angle
- front-door → Entrance, motion-triggered

### SSH

- home-server → 192.168.1.100, user: admin

### TTS

- Preferred voice: "Nova" (warm, slightly British)
- Default speaker: Kitchen HomePod
```

## Why Separate?

Skills are shared. Your setup is yours. Keeping them apart means you can update skills without losing your notes, and share skills without leaking your infrastructure.

### Telegram

- Channel: `telegram`, accountId: `srs` (SRS bot)
- When sending to William's Telegram: `channel: "telegram"`, `accountId: "srs"`

### ACP Working Directory

- My OpenClaw workspace may be a subdirectory like `~/git/srs/openclaw`. That is **my assistant workspace**, not necessarily the real project directory for ACP coding work.
- When delegating project work to ACP agents (Claude/Codex/Gemini/Kiro/etc), do **not** assume the OpenClaw workspace is the project root.
- First ask: **Is the real project directory the parent of the current OpenClaw workspace?** In this setup, yes: workspace is `~/git/srs/openclaw`, while the SRS project directory is the **parent directory** `~/git/srs`.
- Treat this as the default pattern unless the task clearly targets OpenClaw itself: **workspace = assistant home, parent directory = actual project root**.
- For ACP project tasks, prefer the **parent directory of the current workspace** as cwd when that parent is the real repo/project root.
- Use the OpenClaw workspace itself only for OpenClaw-specific/meta tasks.
- The key idea: ACP should see the **project first**, not the assistant's memory/persona files.

### Git Commit Workflow

- **Never `git add`** — William stages files himself
- **Never `git push`** — William pushes himself
- **Commit workflow:** `git diff --cached` → understand the changes → write title/description → `git commit -m "OpenClaw: ..."`
- Title prefix: `OpenClaw:`
- **Co-author for ACP Claude Code:** If Claude Code (ACP) was used to make the changes, add:
  `Co-authored-by: Claude Opus 4.6 <noreply@anthropic.com>`
- **Co-author for ACP Codex:** If Codex (ACP) was used to make the changes, add:
  `Co-authored-by: chatgpt-codex-connector[bot] <199175422+chatgpt-codex-connector[bot]@users.noreply.github.com>`

### Go (GVM)

- Go is managed via **GVM** (Go Version Manager), NOT Homebrew
- Before running any `go` command: `source ~/.gvm/scripts/gvm`
- **Never** use `brew install go` — always use GVM

---

Add whatever helps you do your job. This is your cheat sheet.
