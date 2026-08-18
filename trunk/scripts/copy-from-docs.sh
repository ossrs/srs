#!/bin/bash

# 
# Copy from srs-docs to srs skills.
#

SRS_WORK_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SRS_DOCS="$SRS_WORK_DIR/skills/internal-docs-for-srs/references/cpp-docs"

if [[ ! -d "$SRS_DOCS" ]]; then
  echo "no cpp-docs in $SRS_WORK_DIR"
  exit -1
fi

for target in "$SRS_DOCS/doc/"*.md; do
  source=~/git/srs-docs/for-writers/doc-en-7.0/doc/"$(basename "$target")"
  if [[ ! -f "$source" ]]; then
    continue
  fi
  if ! cp "$source" "$target"; then
    echo "copy doc failed"
    exit -1
  fi
done
echo "Copy doc success"

for target in "$SRS_DOCS/pages/"*.md; do
  source=~/git/srs-docs/for-writers/pages/"$(basename "$target")"
  if [[ ! -f "$source" ]]; then
    continue
  fi
  if ! cp "$source" "$target"; then
    echo "copy pages failed"
    exit -1
  fi
done
echo "Copy pages success"

echo "Done"
