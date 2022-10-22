#!/bin/bash

# cd ~/git/srs
work_dir=$(cd -P $(dirname $0) && cd ../.. && pwd) && cd $work_dir && echo "Run script in ${work_dir}"
if [[ ! -d trunk ]]; then echo "no ./trunk found"; exit 1; fi

echo "For trunk/Dockerfile.test"
docker build --tag srs:test -f trunk/Dockerfile.test . &&
docker build --tag srs:cov -f trunk/Dockerfile.cov .
if [[ $? -ne 0 ]]; then echo "Docker for test failed"; exit 1; fi

echo "For trunk/Dockerfile"
docker buildx build --platform linux/arm/v7 --output "type=image,push=false" --build-arg IMAGE=ossrs/srs:ubuntu20-cache -f trunk/Dockerfile . &&
docker buildx build --platform linux/arm64/v8 --output "type=image,push=false" --build-arg IMAGE=ossrs/srs:ubuntu20-cache -f trunk/Dockerfile . &&
docker buildx build --platform linux/amd64 --output "type=image,push=false" --build-arg IMAGE=ossrs/srs:ubuntu20-cache -f trunk/Dockerfile .
if [[ $? -ne 0 ]]; then echo "Docker for release failed"; exit 1; fi

echo "For trunk/Dockerfile.pkg"
docker build --tag srs:pkg --build-arg version=1.2.3 -f trunk/Dockerfile.pkg .
if [[ $? -ne 0 ]]; then echo "Docker for pkg failed"; exit 1; fi

echo "For trunk/Dockerfile.builds"
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target centos7-baseline . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target centos7-all . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target centos7-no-webrtc . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target centos7-no-asm . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target centos7-ansi-no-ffmpeg . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target centos6-baseline . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target centos6-all . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target ubuntu16-baseline . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target ubuntu16-all . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target ubuntu18-baseline . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target ubuntu18-all . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target ubuntu20-baseline . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target ubuntu20-all . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target ubuntu16-cross-armv7 . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target ubuntu20-cross-armv7 . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target ubuntu16-cross-aarch64 . &&
DOCKER_BUILDKIT=1 docker build -f trunk/Dockerfile.builds --target ubuntu20-cross-aarch64 .
if [[ $? -ne 0 ]]; then echo "Docker for build failed"; exit 1; fi

