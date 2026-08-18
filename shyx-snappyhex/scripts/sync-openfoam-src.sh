#!/usr/bin/env bash
set -euo pipefail
SRC=/home/shyx/src/OpenFOAM-v2412
DEST=/mnt/c/Users/18490/Documents/Github/shyx-snappyhex/third_party/openfoam-v2412
mkdir -p "$DEST"
# Directory must already exist and be NTFS case-sensitive.
cp -a "$SRC/COPYING" "$SRC/README.md" "$SRC/META-INFO" "$DEST/"
cp -a "$SRC/wmake" "$DEST/"
cp -a "$SRC/etc" "$DEST/"
cp -a "$SRC/src" "$DEST/"
mkdir -p "$DEST/applications/utilities"
cp -a "$SRC/applications/utilities/mesh" "$DEST/applications/utilities/"
echo COPY_OK
du -sh "$DEST"
# Verify a known case-conflict pair both exist
ls -la "$DEST/src/OpenFOAM/matrices" | head
