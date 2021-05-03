#!/bin/bash

if [[ ! -d signaling ]]; then
  cd 3rdparty
fi
if [[ ! -d signaling ]]; then
  echo "no 3rdparty"
  exit -1
fi
if [[ ! -d ~/git/signaling ]]; then
  echo "no signaling"
  exit -1
fi

echo "Copy signaling"
cp -R signaling/* ~/git/signaling/ && (cd ~/git/signaling && git st)

echo "Copy httpx-static"
cp -R httpx-static/* ~/git/go-oryx/httpx-static/ && (cd ~/git/go-oryx && git st)

echo "Copy srs-bench"
cp -R srs-bench/* ~/git/srs-bench/ && (cd ~/git/srs-bench && git st)
