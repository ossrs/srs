#!/bin/bash

# cd ~/git/srs
work_dir=$(cd -P $(dirname $0) && cd ../.. && pwd) && cd $work_dir && echo "Run script in ${work_dir}"
if [[ ! -d trunk ]]; then echo "no ./trunk found"; exit 1; fi

echo "For .github/workflows/test.yml"
docker build --tag srs:build -f trunk/Dockerfile.builds . &&
docker build --tag srs:test -f trunk/Dockerfile.test . &&
docker build --tag srs:cov -f trunk/Dockerfile.cov . &&
docker buildx build --platform linux/arm/v7 --output "type=image,push=false" --build-arg IMAGE=ossrs/srs:ubuntu20-cache -f trunk/Dockerfile . &&
docker buildx build --platform linux/arm64/v8 --output "type=image,push=false" --build-arg IMAGE=ossrs/srs:ubuntu20-cache -f trunk/Dockerfile . &&
docker buildx build --platform linux/amd64 --output "type=image,push=false" --build-arg IMAGE=ossrs/srs:ubuntu20-cache -f trunk/Dockerfile .
if [[ $? -ne 0 ]]; then echo "Docker for test failed"; exit 1; fi

echo "For .github/workflows/release.yml"
docker build --tag srs:pkg --build-arg version=1.2.3 -f trunk/Dockerfile.pkg .
if [[ $? -ne 0 ]]; then echo "Docker for release failed"; exit 1; fi

