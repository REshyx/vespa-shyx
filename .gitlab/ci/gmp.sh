#!/bin/sh

set -e

readonly version="6.3.0"

mkdir gmp
cd gmp
mkdir install
curl https://gmplib.org/download/gmp/gmp-$version.tar.xz --output gmp.txz
tar -xvf gmp.txz
cd gmp-$version
./configure --prefix=/builds/vtk/meshing/gmp/install --disable-shared --with-pic
make -j 2
make install
