#!/bin/zsh
set -eu
cd "${0:A:h}"
if [[ ! -x build/infinity ]]; then
  python3 tools/bootstrap.py
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target infinity -j 4
fi
exec ./build/infinity "$@"
