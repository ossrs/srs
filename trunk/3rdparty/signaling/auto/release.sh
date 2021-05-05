#!/bin/bash

SRS_GIT=$HOME/git/signaling
SRS_TAG=

# linux shell color support.
RED="\\033[31m"
GREEN="\\033[32m"
YELLOW="\\033[33m"
BLACK="\\033[0m"

function NICE() {
    echo -e "${GREEN}$@${BLACK}"
}

function TRACE() {
    echo -e "${BLACK}$@${BLACK}"
}

function WARN() {
    echo -e "${YELLOW}$@${BLACK}"
}

function ERROR() {
    echo -e "${RED}$@${BLACK}"
}

##################################################################################
##################################################################################
##################################################################################
if [[ -z $SRS_TAG ]]; then
  SRS_TAG=`(cd $SRS_GIT && git describe --tags --abbrev=0 --exclude release-* 2>/dev/null)`
  if [[ $? -ne 0 ]]; then
    echo "Invalid tag $SRS_TAG of $SRS_FILTER in $SRS_GIT"
    exit -1
  fi
fi

NICE "Build docker for $SRS_GIT, tag is $SRS_TAG"

git ci -am "Release $SRS_TAG"

# For aliyun hub.
NICE "aliyun hub release-v$SRS_TAG"

echo "git push aliyun"
git push aliyun

git tag -d release-v$SRS_TAG 2>/dev/null
echo "Cleanup tag $SRS_TAG for aliyun"

git tag release-v$SRS_TAG; git push -f aliyun release-v$SRS_TAG
echo "Create new tag $SRS_TAG for aliyun"
echo ""

NICE "aliyun hub release-vlatest"
git tag -d release-vlatest 2>/dev/null
echo "Cleanup tag latest for aliyun"

git tag release-vlatest; git push -f aliyun release-vlatest
echo "Create new tag latest for aliyun"

# For github.com
echo "git push origin"
git push origin

echo "git push origin $SRS_TAG"
git push origin $SRS_TAG

NICE "Update github ok"
