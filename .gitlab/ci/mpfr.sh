#!/bin/sh

set -e

readonly version="4.2.1"
   
mkdir mpfr
cd mpfr
mkdir install
curl https://www.mpfr.org/mpfr-current/mpfr-$version.tar.xz --output mpfr.txz
tar -xvf mpfr.txz
cd mpfr-$version
./configure --prefix=/builds/vtk/meshing/mpfr/install --with-gmp=/builds/vtk/meshing/gmp/install --disable-shared --with-pic
make -j 2
make install
