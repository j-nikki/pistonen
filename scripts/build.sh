#!/bin/bash

([ ! -e build ] || rm -rf build) &&
	# mkdir build && cd build &&
	conan install -b missing -b outdated . -of build &&
	cmake -B build "$@" &&
	cmake --build build

# after this, you can do
# cmake --build build
# to build the project
