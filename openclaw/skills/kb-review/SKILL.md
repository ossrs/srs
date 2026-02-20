---
name: kb-review
description: Review and correct SRS knowledge base documents (memory/srs-*.md) by loading relevant source code into context and identifying inaccuracies. Use when asked to review, correct, verify, or check the knowledge base, documentation accuracy, or when someone wants to find issues in srs-overview.md or srs-coroutines.md sections.
---

# KB Review — Knowledge Base Accuracy Checker

Review SRS knowledge base documents against the actual codebase to find inaccuracies.

## Setup

Do **not** hardcode an absolute SRS path. Resolve `SRS_ROOT` dynamically:

1. If `SRS_ROOT` env is set and contains `trunk/src`, use it.
2. Else, if current workspace (or its git root) contains `trunk/src`, use that.
3. Else, if `~/git/srs/trunk/src` exists, use `~/git/srs`.
4. Else, ask the user for the SRS repo root.

All paths below are relative to `$SRS_ROOT`.

**Key paths:**
- Knowledge base: `memory/srs-*.md` (in the workspace/openclaw dir)
- SRS source: `trunk/src/` (subdirs: app, core, kernel, protocol, utest, main)
- ST library: `trunk/3rdparty/st-srs/`
- Config: `trunk/conf/full.conf`

## Workflow

**Step 1: Identify target document and section**

List files matching `memory/srs-*.md` and present them to the user. Ask the user which document and which section to review — let the user type the section name freely (do not list sections for them to pick from).

**Step 2: Read the document section**

Read the chosen section text fully.

**Step 3: Identify and load all relevant source code**

Analyze the section content — every function name, struct, config directive, protocol, file, or mechanism mentioned. Then:

1. Determine which part of the codebase the section covers
2. Use the appropriate skill to load the code — e.g., `st-develop` skill for ST/coroutine code
3. Follow that skill's loading instructions fully — do not skip files or read partially
4. If no skill exists for the relevant codebase area, search and load the files directly

The goal: have every piece of code the section describes loaded in context before reviewing.

**Step 4: Review and report issues**

Compare every claim in the document against the loaded source code. Check for:

- **Factual errors** — Function names, struct names, variable names that don't match code
- **Outdated info** — Behavior described that no longer matches current implementation
- **Missing context** — Important details in the code not mentioned in the doc
- **Wrong mechanics** — Incorrect description of how something works vs what the code actually does
- **Version/date errors** — Wrong version numbers or dates (cross-check git tags if needed)
- **Config errors** — Wrong config directive names, wrong default values

Present findings as a numbered list. For each issue:
1. Quote the problematic text from the doc
2. Explain what the code actually shows
3. Cite the specific file and line(s)

If the section is accurate, say so — don't invent issues.

**Step 5: Ask if user wants corrections applied**

After presenting issues, ask if the user wants to apply fixes to the document. Only edit with explicit approval.
