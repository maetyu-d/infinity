#!/bin/zsh
cd "${0:A:h}"
if [[ -x 'Dune Breaker.app/Contents/MacOS/DuneBreaker' ]]; then
  exec 'Dune Breaker.app/Contents/MacOS/DuneBreaker' --ellipsoid-demo "$@"
fi
exec ./build/dune_breaker --ellipsoid-demo "$@"
