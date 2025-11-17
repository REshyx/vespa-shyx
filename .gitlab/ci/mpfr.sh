#!/bin/sh

set -e

readonly version="4.2.2"
readonly sha256sum="b67ba0383ef7e8a8563734e2e889ef5ec3c3b898a01d00fa0a6869ad81c6ce01"
   
mkdir mpfr
cd mpfr
mkdir install
curl https://www.mpfr.org/mpfr-$version/mpfr-$version.tar.xz --output mpfr.txz
echo "$sha256sum mpfr.txz" > mpfr.sha256sum
sha256sum --check mpfr.sha256sum
tar -xvf mpfr.txz
cd mpfr-$version
./configure --prefix=/builds/vtk/meshing/mpfr/install --with-gmp=/builds/vtk/meshing/gmp/install --disable-shared --with-pic
make -j 2
make install
