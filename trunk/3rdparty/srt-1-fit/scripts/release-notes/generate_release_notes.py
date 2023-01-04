import enum

import click
import numpy as np
import pandas as pd


@enum.unique
class Area(enum.Enum):
    core = 'core'
    tests = 'tests'
    build = 'build'
    apps = 'apps'
    docs = 'docs'


def define_area(msg):
    areas = [e.value for e in Area]

    for area in areas:
        if msg.startswith(f'[{area}] '):
            return area

    return np.NaN


def delete_prefix(msg):
    prefixes = [f'[{e.value}] ' for e in Area]

    for prefix in prefixes:
        if msg.startswith(prefix):
            return msg[len(prefix):]

    return msg[:]


def write_into_changelog(df, f):
    f.write('\n')
    for _, row in df.iterrows():
        f.write(f"\n{row['commit']} {row['message']}")
    f.write('\n')


@click.command()
@click.argument(
    'git_log',
    type=click.Path(exists=True)
)
def main(git_log):
    """
    Script designed to generate release notes template with main sections,
    contributors list, and detailed changelog out of .csv SRT git log file.
    """
    df = pd.read_csv(git_log, sep = '|', names = ['commit', 'message', 'author', 'email'])
    df['area'] = df['message'].apply(define_area)
    df['message'] = df['message'].apply(delete_prefix)

    # Split commits by areas
    core = df[df['area']==Area.core.value]
    tests = df[df['area']==Area.tests.value]
    build = df[df['area']==Area.build.value]
    apps = df[df['area']==Area.apps.value]
    docs = df[df['area']==Area.docs.value]
    other = df[df['area'].isna()]

    # Define individual contributors
    contributors = df.groupby(['author', 'email'])
    contributors = list(contributors.groups.keys())

    with open('release-notes.md', 'w') as f:
        f.write('# Release Notes\n')

        f.write('\n## API / ABI / Integration Changes\n')
        f.write('\n**API/ABI version: 1.x.**\n')

        f.write('\n## New Features and Improvements\n')
        f.write('\n## Important Bug Fixes\n')
        f.write('\n## Build\n')
        f.write('\n## Documentation\n')

        f.write('\n## Contributors\n')
        for name, email in contributors:
            f.write(f'\n{name} <{email}>')
        f.write('\n')

        f.write('\n## Changelog\n')
        f.write('\n<details><summary>Click to expand/collapse</summary>')
        f.write('\n<p>')
        f.write('\n')

        if not core.empty:
            f.write('\n### Core Functionality')
            write_into_changelog(core, f)

        if not tests.empty:
            f.write('\n### Unit Tests')
            write_into_changelog(tests, f)

        if not build.empty:
            f.write('\n### Build Scripts (CMake, etc.)')
            write_into_changelog(build, f)

        if not apps.empty:
            f.write('\n### Sample Applications')
            write_into_changelog(apps, f)

        if not docs.empty:
            f.write('\n### Documentation')
            write_into_changelog(docs, f)

        if not other.empty:
            f.write('\n### Other')
            write_into_changelog(other, f)

        f.write('\n</p>')
        f.write('\n</details>')


if __name__ == '__main__':
    main()
