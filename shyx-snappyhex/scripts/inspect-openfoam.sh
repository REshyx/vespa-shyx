#!/usr/bin/env bash
set -euo pipefail
OF=/home/shyx/src/OpenFOAM-v2412
echo '=== OSspecific/MSwindows ==='
ls "$OF/src/OSspecific/MSwindows"
echo '=== MSwindows Make/files ==='
cat "$OF/src/OSspecific/MSwindows/Make/files"
echo '=== Pstream ==='
ls "$OF/src/Pstream"
echo '=== flex .L ==='
find "$OF/src" -name '*.L'
echo '=== wmake win rules ==='
ls "$OF/wmake/rules" | grep -iE 'mingw|w64|win' || true
echo '=== snappy options ==='
cat "$OF/src/mesh/snappyHexMesh/Make/options"
echo '=== finiteVolume Make/files line count ==='
wc -l "$OF/src/finiteVolume/Make/files" "$OF/src/dynamicMesh/Make/files" "$OF/src/meshTools/Make/files" "$OF/src/fileFormats/Make/files" "$OF/src/surfMesh/Make/files" "$OF/src/sampling/Make/files" "$OF/src/overset/Make/files" "$OF/src/fvMotionSolver/Make/files" "$OF/src/lagrangian/basic/Make/files" "$OF/src/parallel/decompose/decompositionMethods/Make/files" "$OF/src/parallel/distributed/Make/files" 2>/dev/null || true
