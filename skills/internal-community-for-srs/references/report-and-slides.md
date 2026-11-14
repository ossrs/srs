# Report and Slides

Summarize what changed in SRS during the period, as a text report in the reply and a slide deck to share.

## Step 1: Collect the commits

Use the SRS repository in the current working directory. List the commits of the period on each branch:

- `develop`, for new features and fixes.
- Each maintained release branch, such as `7.0release` and `6.0release`, for picks and releases.

```bash
git log develop --since=<start> --until=<end+1 day> --format='%h %ad %an %s' --date=short
git log develop --since=<start> --until=<end+1 day> --format='%an' | sort | uniq -c
git diff --shortstat <first>~1 <last>
```

Then read the full message of every commit that is not a release commit (`git log -1 --format=%B <hash>`). Pull request descriptions in the squashed commits carry the problem, the fix and the tests.

## Step 2: Understand and group

- Group the changes into a few themes, such as quality, new platforms, tools, security, user-reported fixes and release branches.
- For each theme, keep what a user notices: what was broken or missing, what works now, and the version.
- Note breaking changes, such as removed features or changed config, and the upgrade advice.
- Credit outside contributors and reporters by their GitHub name.
- When two sources disagree, such as a title and a description, say so instead of picking one.

## Step 3: Text report

Answer in the reply first:

- The numbers: commits per branch, releases, versions, and contributors.
- One section per theme with short bullets that link each change to its version and pull request.
- What was not covered, such as work not yet committed.

## Step 4: Slides

- Use the Slides artifact type when the Artifact tool offers one; otherwise write one HTML file.
- About 10 to 15 slides:
  1. Cover with the period and a one-line headline.
  2. The numbers.
  3. The releases.
  4. One slide per theme.
  5. Thanks to contributors and reporters.
  6. What users should do now, such as upgrade.
- One idea per slide, with speaker notes holding the details.
- Give the user the link, and remind them that the deck is private until they share it.
