# Scan Issues

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Scan Issues**. Do not execute it directly.

1. Select exactly one issue repository: `ossrs/srs` or `ossrs/oryx`. Use an explicit issue URL, product name, or established task context; ask when ambiguous.
2. Scan that repository's open issues by GitHub `updated_at`, newest first; do not rely on bug labels.
3. Read `references/srs-issues.md` for SRS or `references/oryx-issues.md` for Oryx, then read the issue and comments. Find the latest authorized Truth Record for the selected project.
4. Skip it when that record is current. Select it as `NO_TRUTH` when none exists, or `UPDATED` when later issue content or comments exist. Metadata-only changes do not count.
5. Continue until the requested count (default five). Return the selected repository, each issue link, status, latest meaningful activity, and one-line reason.

Do not modify issues or create Truth Records.
