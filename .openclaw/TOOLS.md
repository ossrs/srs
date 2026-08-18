# TOOLS.md - Local Notes

Skills define _how_ tools work. This file is for _your_ specifics — the stuff that's unique to your setup.

## Model Auth

- Anthropic / Opus refresh: `claude setup-token` -> `openclaw models auth setup-token --provider anthropic`
- Codex refresh: `openclaw models auth login --provider openai-codex`
- Temporary workaround when one model auth is broken: use `/model ...` in the current session to switch to another working model.

### Telegram

- Channel: `telegram`, accountId: `srs` (SRS bot)
- When sending to William's Telegram: `channel: "telegram"`, `accountId: "srs"`

### Working Directory

- ⚠️ **CRITICAL RULE:** Find everything from the current working directory. All SRS project directories are available here — no discovery, no parent traversal, no absolute paths.
- Available directories: `trunk/`, `cmd/`, `internal/`, `cmake/`, `docs/`, `memory/`, `skills/`
- All AI tools (OpenClaw, Codex, Claude Code, Kiro CLI) see the same relative paths.
- ACP agents (Codex, Claude Code, etc.) also use the current directory as root — they find files from here too.
- Use the OpenClaw workspace itself only for OpenClaw-specific/meta tasks.

---

Add whatever helps you do your job. This is your cheat sheet.
