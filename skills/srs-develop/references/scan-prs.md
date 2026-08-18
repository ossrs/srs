# Scan PRs

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Scan PRs**. Do not execute it directly.

1. Select exactly one pull-request repository: `ossrs/srs` or `ossrs/oryx`. Use an explicit URL, product name, or established task context; ask when ambiguous.
2. Prefer a pull request ID or URL from the user and scan only that pull request. If none is provided, scan only the single open pull request with the newest GitHub `updated_at` in the selected repository. Do not bulk-scan multiple pull requests.
3. Read the complete body, commit history, code diff, comments, reviews and review threads, and checks. Do not rely on labels, review state, or the author's summary. Use the selected project's documentation and code map to inspect only the relevant current code.
4. Assess the pull request from that complete current state. Look for correctness problems, regressions, compatibility impact, missing tests or documentation, unresolved feedback, and failing or incomplete checks.
5. Return the repository, pull request link, change summary, latest meaningful activity, checks and review state, findings, and one-line reason it needs maintainer attention.

Pull requests have no Truth Record. Do not invent one or treat any comment, review, or summary as authoritative; reconcile every source with the code change and current project state.

Do not modify, comment on, or submit reviews for pull requests.
