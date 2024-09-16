#!/bin/sh

set -e

readonly version="5.6.1"
   
mkdir cgal
cd cgal
git clone --depth 1 -b v$version https://github.com/CGAL/cgal.git src
mkdir build
mkdir install
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install -S ../src
cmake --build . --parallel 2
cmake --install .
