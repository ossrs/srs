#!/bin/bash

if [[ ! -d 3rdparty/signaling || ! -d 3rdparty/httpx-static || ! -d 3rdparty/srs-bench ]]; then
  echo "no signaling or httpx-static or srs-bench in $(pwd)"
  exit -1
fi
if [[ ! -d ~/git/signaling || ! -d ~/git/go-oryx/httpx-static || ! -d ~/git/srs-bench ]]; then
  echo "no signaling or httpx-static or srs-bench at ~/git"
  exit -1
fi

if [[ ! -f ~/git/srs-bench/go.mod ]]; then
  echo "no feature/rtc in srs-bench"
  exit -1
fi

echo "Copy signaling"
cp -R 3rdparty/signaling/* ~/git/signaling/ &&
cp -R 3rdparty/signaling/.gitignore ~/git/signaling/ &&
(cd ~/git/signaling && git st)

echo "Copy httpx-static"
cp -R 3rdparty/httpx-static/* ~/git/go-oryx/httpx-static/ &&
cp -R 3rdparty/httpx-static/.gitignore ~/git/go-oryx/httpx-static/ &&
(cd ~/git/go-oryx && git st)

echo "Copy srs-bench"
cp -R 3rdparty/srs-bench/* ~/git/srs-bench/ &&
cp -R 3rdparty/srs-bench/.gitignore ~/git/srs-bench/ &&
(cd ~/git/srs-bench && git st)

