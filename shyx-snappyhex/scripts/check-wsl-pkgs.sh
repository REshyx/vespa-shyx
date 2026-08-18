#!/usr/bin/env bash
set -euo pipefail
echo "bison=$(command -v bison || true)"
echo "flex=$(command -v flex || true)"
echo "g++=$(g++ --version | head -1)"
echo "make=$(make --version | head -1)"
dpkg -l zlib1g-dev libscotch-dev bison flex 2>/dev/null | tail -n +6 || true
