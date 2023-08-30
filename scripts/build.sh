#!/bin/bash

btype="Debug"
if [ "$1" = "--release" ]; then
    shift
    btype="Release"
fi

([ ! -e build ] || rm -rf build) && mkdir build &&
# conan install -b '*' --profile cpp23 --profile:build=cpp23 . -of build -s build_type=$btype &&
conan install -b missing --profile cpp23 --profile:build=cpp23 -b outdated . -of build -s build_type=$btype &&
sed -i'' -Ee's/"conan-(debug|release)"/"conan"/' build/CMakePresets.json &&
cmake -B build "$@" -DCMAKE_BUILD_TYPE=$btype --preset $btype -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake &&
cmake --build build

# after this, you can do
# cmake --build build
# to build the project
