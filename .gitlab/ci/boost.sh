#!/bin/sh

set -e

readonly version="1.86.0"
   
mkdir boost
cd boost
git clone --depth 1 -b boost-$version https://github.com/boostorg/boost.git src
cd src
git submodule update --init --recursive
cd ..
mkdir build
mkdir install
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install -S ../src
cmake --build . --parallel 2
cmake --install .
