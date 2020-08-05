#!/bin/bash -e

#. sdk.sh

#[ ! -d irrlicht-src ] && \
#	git clone -b ogl-es --depth 1 https://github.com/MoNTE48/Irrlicht irrlicht-src

cd irrlicht-src/source/Irrlicht/MacOSX
xcodebuild build \
	-project MacOSX.xcodeproj \
	-configuration Release \
	-scheme libIrrlicht.a
BUILD_FOLDER=$(xcodebuild -project MacOSX.xcodeproj -scheme \
        libIrrlicht.a -showBuildSettings |\
        grep TARGET_BUILD_DIR | sed -n -e 's/^.*TARGET_BUILD_DIR = //p')
cd ../../..

#[ -d ../irrlicht ] && rm -r ../irrlicht
mkdir -p ../irrlicht
#cp lib/iOS/libIrrlicht.a ../irrlicht/
cp "${BUILD_FOLDER}/libIrrlicht.a" ../irrlicht
#Avoid rebuild everytime
#cp -r include ../irrlicht/include
#cp -r media/Shaders ../irrlicht/shaders

echo "Irrlicht build successful"
