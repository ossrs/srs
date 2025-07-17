---
title: Git
sidebar_label: Git
hide_title: false
hide_table_of_contents: false
---

# Git Usage

How to use stable version of SRS? How to update code?

## Checkout Branch

Some features are introduced in SRS2.0, the SRS1.0 does not support.
The wiki url specifies the version of SRS supports it.

To checkout SRS1.0 branch:

```
git pull && git checkout 1.0release
```

To checkout SRS2.0 branch:

```
git pull && git checkout 2.0release
```

To checkout SRS3.0 branch:

```
git pull && git checkout 3.0release
```

To checkout SRS4.0 branch:

```
git pull && git checkout 4.0release
```

To checkout SRS5.0 branch(if no 5.0release branch, it's develop):

```
git pull && git checkout develop
```

## SRS Branches

The release branch is more stable than develop.

* 3.0release, stable release branch.
* 4.0release, stable release branch.
* develop(5.0), not stable.

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/git)


