# Scan Issues

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Scan Issues**. Do not execute it directly.

1. Scan open issues by GitHub `updated_at`, newest first; do not rely on bug labels.
2. Read `references/issues.md`, then the issue and comments. Find the latest authorized Truth Record.
3. Skip it when that record is current. Select it as `NO_TRUTH` when none exists, or `UPDATED` when later issue content or comments exist. Metadata-only changes do not count.
4. Continue until the requested count (default five). Return each issue link, status, latest meaningful activity, and one-line reason.

Do not modify issues or create Truth Records.
