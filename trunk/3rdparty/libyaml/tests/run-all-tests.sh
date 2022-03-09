#!/bin/sh

set -e

main() {
  # Autoconf based in-source build and tests
  clean

  ./bootstrap
  ./configure
  make test-all

  # CMake based in-source build and tests
  clean

  cmake .
  make
  make test

  clean
}

clean() {
  git clean -d -x -f
  rm -fr tests/run-test-suite
  git worktree prune
}

main "$@"
