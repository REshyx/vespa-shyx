#!/bin/sh

set -e

readonly version="3.4.0"
   
mkdir eigen
cd eigen
git clone --depth 1 -b $version https://gitlab.com/libeigen/eigen.git src
mkdir build
mkdir install
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install -S ../src
cmake --build . --parallel 2
cmake --install .
