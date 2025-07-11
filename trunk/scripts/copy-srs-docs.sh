#!/bin/bash

if [[ ! -d 3rdparty/srs-docs ]]; then
  echo "no srs-docs in $(pwd)"
  exit -1
fi

mkdir -p 3rdparty/srs-docs/blog &&
cp ~/git/srs-docs/for-writers/blog-en/*.md 3rdparty/srs-docs/blog/
if [[ $? -ne 0 ]]; then
  echo "copy blog failed"
  exit -1
fi
echo "Copy blog success"

mkdir -p 3rdparty/srs-docs/doc &&
cp ~/git/srs-docs/for-writers/doc-en-7.0/doc/*.md 3rdparty/srs-docs/doc/
if [[ $? -ne 0 ]]; then
  echo "copy doc failed"
  exit -1
fi
echo "Copy doc success"

mkdir -p 3rdparty/srs-docs/pages &&
cp ~/git/srs-docs/for-writers/pages/*-en.md 3rdparty/srs-docs/pages/ &&
if [[ $? -ne 0 ]]; then
  echo "copy pages failed"
  exit -1
fi
echo "Copy pages success"

echo "Done"
