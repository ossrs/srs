# Script Description

Script designed to generate release notes template with main sections, contributors list, and detailed changelog out of `.csv` SRT git log file. The output `release-notes.md` file is generated in the root folder.

In order to obtain the git log file since the previous release (e.g., v1.4.0), use the following command:

```
git log --pretty=format:"%h|%s|%an|%ae" v1.4.0...HEAD^ > commits.csv
```

## Requirements

* Python 3.6+

To install Python libraries use:
```
pip install -r requirements.txt
```
