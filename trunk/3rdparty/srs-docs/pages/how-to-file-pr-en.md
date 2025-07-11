# HowToFilePR

Thank you for your PR, please follow this guide.

## Rules

* Never use your `develop` branch, use `bugfix/bug-summary` for each PR.
* Don't close PR when update, only update the branch `bugfix/bug-summary`, simple enough.
* Be focus, one PR fixes exactly one bug/feature, without any noise like space or dead codes.
* Please study [Pro Git](https://git-scm.com/book/en/v2), it's a very important and basic skill for open-source developer.

## File New PR

The workflow to patch `develop` or any other branches:

[![Workflow](/img/HowToFilePR.png)](https://www.figma.com/file/5yAeoq2r3wwrXZwq1f93UH/How-to-File-PR-to-SRS)

**Step 1:** Fork SRS

Open [ossrs/srs](https://github.com/ossrs/srs), click `Fork` to your repository.

**Step 2:** Clone your repository

```
git clone git@github.com:your-account/srs.git
git checkout -b master origin/master
cd srs
```

> Note: You should setup your git `user.name` and `user.email`.

**Step 3:** Add a remote srs

```
git remote add srs https://github.com/ossrs/srs.git
git fetch srs
```

**Step 4:** Sync with remote before each PR

```
git fetch --all
```

**Step 5:** Checkout a new branch from srs

```
git checkout -b bugfix/bug-summary srs/develop
```

> Note: Please name your branch, by summary of bug, for example `bugfix/rtc-listen-ipv6`

**Step 6:** Update and push to your repository

```
git push -u origin bugfix/bug-summary
```

> Note: Please use English in code, logs, commit and other text.

**Step 7:** File a [PR](https://github.com/ossrs/srs/compare) from your `bugfix/bug-summary` to SRS `develop`

> Remark: Please check the `Allow edits and access to secrets by maintainers`, so we could update the PR.

## Update Your PR

After review, you might need to update your PR:

```
git checkout bugfix/bug-summary
git commit -am 'Description for update'
git push
```

> Note: Don't file a new PR, what you need to do is to commit to your branch, the PR will be updated automatically by GitHub.

## Setup Your Email

Please setup your [GitHub: Email](https://github.com/settings/emails), please **DONOT** select the `Keep my email addresses private`, which makes the commit with strange email address.

And setup user for `git commit` by:

```bash
cd ~/git/srs
git config --local user.name "username"
git config --local user.email "useremail@xxx.com"
git config --list
```

Please setup these settings to ensure you're in the [SRS: Contributors](https://github.com/ossrs/srs/graphs/contributors).

## TOC: Update PR

Generally, TOC who has write access to SRS also are able to update the PR, please read [this post](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/committing-changes-to-a-pull-request-branch-created-from-a-fork).

Let's take a example of [PR #2908](https://github.com/ossrs/srs/pull/2908):

* Title: `SRT: url supports multiple QueryStrings`
* Fork: `https://github.com/zhouxiaojun2008/srs/tree/bugfix/fix-srt-url`
* Branch: `bugfix/fix-srt-url`

**Step 1:** Add a remote of PR fork, use SSH to clone the fork repository.

```bash
git remote add tmp git@github.com:zhouxiaojun2008/srs.git
```

**Step 2:** Update the fork repository, to get the branch.

```bash
git fetch tmp
```

**Step 3:** Now we got the branch of PR, switch to it.

```bash
git checkout bugfix/fix-srt-url
```

**Step 4:** Please update the branch, then push to the fork repository.

```bash
git push tmp bugfix/fix-srt-url
```

The PR should be updated automatically.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/pages/how-to-file-pr-en)


