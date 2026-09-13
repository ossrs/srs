#!/bin/bash

#
# Copy from SRS skills to SRS Docs2.
#

SRS_WORK_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SRS_DOCS="$SRS_WORK_DIR/skills/internal-docs-for-srs/references/cpp-docs"
SRS_ORYX_DOCS="$SRS_WORK_DIR/skills/internal-docs-for-srs/references/oryx"
SRS_DOCS2="$SRS_WORK_DIR/website"
SRS_DOCS2_CURRENT="$SRS_DOCS2/docs"
SRS_DOCS2_PAGES="$SRS_DOCS2/src/pages"
SRS_DOCS2_BLOG="$SRS_DOCS2/blog"

docs2_page_path() {
  case "$1" in
    faq-server-en.md) echo "$SRS_DOCS2_PAGES/faq.md" ;;
    *-en.md) echo "$SRS_DOCS2_PAGES/${1%-en.md}.md" ;;
    *) echo "$SRS_DOCS2_PAGES/$1" ;;
  esac
}

if [[ ! -d "$SRS_DOCS" ]]; then
  echo "no cpp-docs in $SRS_WORK_DIR"
  exit -1
fi

if [[ ! -d "$SRS_ORYX_DOCS" ]]; then
  echo "no oryx docs in $SRS_WORK_DIR"
  exit -1
fi

if [[ ! -d "$SRS_DOCS2_CURRENT" || ! -d "$SRS_DOCS2_PAGES" || ! -d "$SRS_DOCS2_BLOG" ]]; then
  echo "no SRS Docs2 project in $SRS_DOCS2"
  exit -1
fi

for source in "$SRS_DOCS/doc/"*.md; do
  target="$SRS_DOCS2_CURRENT/doc/$(basename "$source")"
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
  target=$(docs2_page_path "$(basename "$source")")
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
  page_target=$(docs2_page_path "$name")
  for target in \
    "$SRS_DOCS2_CURRENT/doc/$name" \
    "$page_target" \
    "$SRS_DOCS2_BLOG/$name"; do
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
