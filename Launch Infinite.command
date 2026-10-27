#!/bin/zsh
set -eu
cd "${0:A:h}"
if [[ ! -x build/infinite ]]; then
  python3 tools/bootstrap.py
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target infinite -j 4
fi
exec ./build/infinite "$@"
