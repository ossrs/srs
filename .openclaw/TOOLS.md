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

## Model Auth

- Anthropic / Opus refresh: `claude setup-token` -> `openclaw models auth setup-token --provider anthropic`
- Codex refresh: `openclaw models auth login --provider openai-codex`
- Temporary workaround when one model auth is broken: use `/model ...` in the current session to switch to another working model.

### Telegram

- Channel: `telegram`, accountId: `srs` (SRS bot)
- When sending to William's Telegram: `channel: "telegram"`, `accountId: "srs"`

### Working Directory

- ⚠️ **CRITICAL RULE:** Find everything from the current working directory. All SRS project directories are available here — no discovery, no parent traversal, no absolute paths.
- Available directories: `trunk/`, `cmd/`, `internal/`, `cmake/`, `docs/`, `memory/`
- All AI tools (OpenClaw, Codex, Claude Code, Kiro CLI) see the same relative paths.
- ACP agents (Codex, Claude Code, etc.) also use the current directory as root — they find files from here too.
- Use the OpenClaw workspace itself only for OpenClaw-specific/meta tasks.

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
