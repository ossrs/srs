#!/bin/bash

#
# Copy from srs skills to srs-docs.
#

SRS_WORK_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SRS_DOCS="$SRS_WORK_DIR/skills/internal-docs-for-srs/references/cpp-docs"
SRS_ORYX_DOCS="$SRS_WORK_DIR/skills/internal-docs-for-srs/references/oryx"
SRS_DOCS_SOURCE=~/git/srs-docs/for-writers
SRS_DOCS_CURRENT=~/git/srs-docs/i18n/en-us/docusaurus-plugin-content-docs/current

if [[ ! -d "$SRS_DOCS" ]]; then
  echo "no cpp-docs in $SRS_WORK_DIR"
  exit -1
fi

if [[ ! -d "$SRS_ORYX_DOCS" ]]; then
  echo "no oryx docs in $SRS_WORK_DIR"
  exit -1
fi

for source in "$SRS_DOCS/doc/"*.md; do
  target="$SRS_DOCS_CURRENT/doc/$(basename "$source")"
  if [[ ! -f "$target" ]]; then
    continue
  fi
  if ! cp "$source" "$target"; then
    echo "copy doc failed"
    exit -1
  fi
done
echo "Copy doc success"

for source in "$SRS_DOCS/pages/"*.md; do
  target="$SRS_DOCS_SOURCE/pages/$(basename "$source")"
  if [[ ! -f "$target" ]]; then
    continue
  fi
  if ! cp "$source" "$target"; then
    echo "copy pages failed"
    exit -1
  fi
done
echo "Copy pages success"

for source in "$SRS_ORYX_DOCS/"*.md; do
  name=$(basename "$source")
  for target in \
    "$SRS_DOCS_CURRENT/doc/$name" \
    "$SRS_DOCS_SOURCE/pages/$name" \
    "$SRS_DOCS_SOURCE/blog-en/$name"; do
    if [[ ! -f "$target" ]]; then
      continue
    fi
    if ! cp "$source" "$target"; then
      echo "copy oryx docs failed"
      exit -1
    fi
    break
  done
done
echo "Copy oryx docs success"

echo "Done"
