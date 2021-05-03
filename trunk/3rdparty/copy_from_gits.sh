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
cp -R ~/git/signaling/* signaling/

echo "Copy httpx-static"
cp -R ~/git/go-oryx/httpx-static/* httpx-static/

echo "Copy srs-bench"
cp -R ~/git/srs-bench/* srs-bench/
