#!/bin/sh

set -e

# We use an older gmp because of an illegal instruction issue
# https://gmplib.org/list-archives/gmp-bugs/2021-January/004989.html
readonly version="5.1.3"
readonly sha256sum="dee2eda37f4ff541f30019932db0c37f6f77a30ba3609234933b1818f9b07071"

mkdir gmp
cd gmp
mkdir install
curl --connect-timeout 5 --max-time 10 --retry 5 --retry-delay 0 --retry-max-time 40 https://gmplib.org/download/gmp/gmp-$version.tar.xz --output gmp.txz
echo "$sha256sum gmp.txz" > gmp.sha256sum
sha256sum --check gmp.sha256sum
tar -xvf gmp.txz
cd gmp-$version
./configure --prefix=/builds/vtk/meshing/gmp/install --disable-shared --with-pic
make -j 2
make install
