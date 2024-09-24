#!/bin/sh

set -e

# We use an older gmp because of an illegal instruction issue
# https://gmplib.org/list-archives/gmp-bugs/2021-January/004989.html
readonly version="5.1.3"

mkdir gmp
cd gmp
mkdir install
curl https://gmplib.org/download/gmp/gmp-$version.tar.xz --output gmp.txz
tar -xvf gmp.txz
cd gmp-$version
./configure --prefix=/builds/vtk/meshing/gmp/install --disable-shared --with-pic
make -j 2
make install
