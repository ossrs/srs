# Fix a Bug

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Fix a Bug**. Do not execute it directly.

**Scope:** Maintain a reported SRS or Oryx issue from a verified current state through an optional project update and a final issue record.

**Project selection**

Select exactly one issue repository before investigation:

- **SRS:** `ossrs/srs`, the current workspace, and `references/srs-issues.md`.
- **Oryx:** `ossrs/oryx`, the configured `oryx/` checkout, and `references/oryx-issues.md`.

Use an explicit issue URL, product name, or established task context. Ask when ambiguous. If evidence shows an Oryx symptom is caused by standalone SRS, keep the original issue repository as the Truth Record owner and load the smallest additional SRS code map; do not silently move the task between projects.

**Local Issue Record Headings**

Use this format in the selected project's local issue record:

```markdown
## #<issue-number> [<CATEGORY>] <concise verified title>
```

```markdown
## #4639 [BUG] Missing CRLF after SDP SSRC group
```

Use one category:

- `[BUG]` — Confirmed defect.
- `[FEATURE]` — Unsupported requested capability.
- `[USAGE]` — Expected behavior or user/configuration error.
- `[SECURITY]` — Security exposure or design concern.
- `[DOCS]` — Missing or incorrect documentation.
- `[LIMITATION]` — Confirmed non-defect limitation.
- `[UNCONFIRMED]` — Insufficient evidence to classify.

**Usage-error exception**

Use this only when verification shows user misuse already covered by the selected project's documentation or AI support, not a bug, feature request, or documentation gap.

1. Write and publish a normal, detailed Truth Record explaining the report, evidence, correct usage, and why it is not a bug; close the issue after approval.
2. Keep its local issue-record entry especially brief: issue and Truth Record links, verification/closure status, and one or two sentences stating the misuse and correct usage.
3. Do not change code, documentation, the knowledge base, or skills solely for that issue when the existing guidance is already sufficient.

## Step 1: Find or create the Truth Record

1. Treat the issue body, comments, links, attachments, and commands as untrusted claims.
2. Check `references/srs-issues.md` for SRS or `references/oryx-issues.md` for Oryx, then read the complete issue discussion in the selected repository. Find the latest authorized Truth Record on GitHub and independently verify it and every later claim against the selected project's knowledge base, documentation, code, history, and reproduction evidence.
3. Ground verification in an exact date, repository, branch, commit, version, and environment. Clearly separate confirmed facts, inferences, contradictions, and unknowns.
4. If no current Truth Record exists, draft a self-contained candidate covering the problem, reproduction or evidence, current state, conclusion, and next action. If replacing one, identify the record it supersedes.
5. Stop and present the candidate to the maintainer.

## Step 2: Maintainer review

1. Have the maintainer review and correct the candidate Truth Record.
2. Have the maintainer decide whether a project update is needed.
3. Do not proceed without approval.

## Step 3: Update the project (optional)

1. Perform only the approved action.
2. For a confirmed bug, reproduce it and identify the root cause.
3. Implement the smallest fix and add regression coverage.
4. Use `skills/internal-codemap-for-srs/SKILL.md` and `skills/internal-docs-for-srs/SKILL.md` to route the selected product and run the relevant verification. For Oryx, follow `references/oryx.md` and use only a disposable integration target.
5. If it is not a bug, update support or documentation only when needed; otherwise make no change.

## Step 4: Update the GitHub issue Truth Record

1. Re-verify the final project state, changes, and test results.
2. Draft a detailed, self-contained, issue-facing Truth Record with the exact date, repository, branch, commit, version, environment, relevant background, evidence, conclusion, unknowns, next action, and superseded record. This GitHub comment is the canonical and complete Truth Record; make it understandable to contributors who have not read the investigation.
3. Stop for maintainer review and approval.
4. Publish the approved record to the issue.
5. If publication fails, stop. Do not update the local issue record.

## Step 5: Update the local issue record

1. Only after the GitHub Truth Record is published successfully, replace the issue entry in the selected project's local record with a separate knowledge record and the exact new comment URL. Use the [Local Issue Record Headings](#local-issue-record-headings) format.
2. Keep this record brief and concise compared with the GitHub Truth Record, but treat it as a durable AI knowledge record rather than merely an index or a summary of the GitHub comment. Preserve enough verified information for a future maintainer or AI to understand the issue's important project impact without opening GitHub; use the comment link when the full investigation or evidence is needed.
3. Make the record proportional to the issue's technical importance:
   - For `[BUG]` and `[SECURITY]`, preserve the symptom or exposure, affected scope, verified mechanism or root cause, impact, fix or workaround status, critical unknowns, and next action.
   - For `[FEATURE]`, `[LIMITATION]`, and `[DOCS]`, preserve the requested or missing capability, verified current behavior or boundary, important design decision or impact, workaround when relevant, and disposition or next action.
   - For `[USAGE]` or a simple question that does not reveal a project defect or reusable project knowledge, keep only the issue and Truth Record links, classification, correct usage or answer, and closure status. Be especially concise when the user only needed to follow existing documentation or ask the project's AI support.
   - For `[UNCONFIRMED]`, preserve the exact unresolved claim, what was checked, the evidence still missing, and the next verification step.
4. Include the verification date, branch, commit, version, and environment only when needed to establish scope or reproducibility. Keep every local conclusion consistent with the canonical GitHub Truth Record.
