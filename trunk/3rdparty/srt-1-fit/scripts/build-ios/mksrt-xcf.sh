#!/bin/zsh

# Determine the path of the executing script
BASE_DIR=$(readlink -f $0 | xargs dirname)

srt="../.."
ssl=$BASE_DIR/OpenSSL
out=$BASE_DIR/libsrt
target=13.0
tv_target=14.0
tvos_sdk=`xcrun -sdk appletvos --show-sdk-version`
lib=libsrt.a

ios_archs=('arm64' 'arm64' 'x86_64' 'arm64' 'arm64' 'x86_64')
ios_platforms=('OS' 'ARM_SIMULATOR' 'SIMULATOR64' 'TV' 'TV_ARM_SIMULATOR' 'TV_SIMULATOR' )
ios_targets=('iOS-arm64' 'iOS-simArm' 'iOS-simIntel64' 'tvOS' 'tvOS_simArm' 'tvOS_simIntel64')
ssl_folders=('iphoneos' 'iphonesimulator' 'iphonesimulator' 'appletvos' 'appletvsimulator' 'appletvsimulator')
rm -rf $out

cd $srt

for idx in {1..$#ios_archs}
do
 ios_arch=${ios_archs[$idx]}
 ios_platform=${ios_platforms[$idx]}
 dest=${ios_targets[$idx]}
 ssl_path=${ssl}/${ssl_folders[$idx]}

 git clean -fd -e scripts
 echo '********************************************************************************'
 echo SSL_PATH $ssl_path ARCH $ios_arch PLATFORM $ios_platform

 CMAKE_GENERATOR=Xcode ./configure \
 --enable-shared=OFF --enable-apps=OFF \
 --cmake-toolchain-file=scripts/iOS.cmake --ios-arch=$ios_arch --ios-platform=$ios_platform \
 --cmake-xcode-attribute-iphoneos-deployment-target=$target \
 --cmake-xcode-attribute-tvos-deployment-target=$tv_target \
 --cmake-prefix-path=$ssl_path --use-openssl-pc=OFF \
 --cmake_install_prefix=$out/$dest

 cmake --build . --target install --config Release
 echo '********************************************************************************'
done

git clean -fd -e scripts

lipo -create $out/iOS-simArm/lib/$lib $out/iOS-simIntel64/lib/$lib -output $out/libsrt-ios-sim.a
lipo -create $out/tvOS_simArm/lib/$lib $out/tvOS_simIntel64/lib/$lib -output $out/libsrt-tv-sim.a

srt_headers=$out/iOS-arm64/include
rm -rf $BASE_DIR/libsrt.xcframework

echo '####### create XCFramework bundle #######'

xcodebuild -create-xcframework \
 -library $out/iOS-arm64/lib/$lib -headers $srt_headers \
 -library $out/libsrt-ios-sim.a -headers $srt_headers \
 -library $out/tvOS/lib/$lib -headers $srt_headers \
 -library $out/libsrt-tv-sim.a -headers $srt_headers \
 -output $BASE_DIR/libsrt.xcframework
