# Script Description

Script designed to generate release notes template with main sections, contributors list, and detailed changelog out of `.csv` SRT git log file. The output `release-notes.md` file is generated in the root folder.

In order to obtain the git log file since the previous release (e.g., v1.4.0), use the following command:

```shell
git log --pretty=format:"%h|%s|%an|%ae" v1.4.0...HEAD > commits.csv
```

Use the produced `commits.csv` file as an input to the script:

```shell
python scripts/release-notes/generate-release-notes.py commits.csv
```

The script produces `release-notes.md` as an output.


## Requirements

* Python 3.6+

To install Python libraries use:
```shell
pip install -r requirements.txt
```
