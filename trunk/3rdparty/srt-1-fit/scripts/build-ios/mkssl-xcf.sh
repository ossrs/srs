#!/bin/zsh

# Determine the path of the executing script
BASE_DIR=$(readlink -f $0 | xargs dirname)

REPO_DIR=OpenSSL
SSL_DIR=$BASE_DIR/$REPO_DIR

if [ ! -d $REPO_DIR ]
then
  git clone https://github.com/krzyzanowskim/OpenSSL
fi
cd $REPO_DIR
make build

rm -rf $BASE_DIR/libcrypto.xcframework
xcodebuild -create-xcframework \
 -library $SSL_DIR/iphoneos/lib/libcrypto.a \
 -library $SSL_DIR/iphonesimulator/lib/libcrypto.a \
 -library $SSL_DIR/appletvos/lib/libcrypto.a \
 -library $SSL_DIR/appletvsimulator/lib/libcrypto.a \
 -output $BASE_DIR/libcrypto.xcframework
