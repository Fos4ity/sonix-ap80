#!/bin/sh
# Builds and runs the crossfade tests. Invoked from the project root as
# `sh tests/run.sh`; it finds the root itself otherwise.
set -e
cd "$(dirname "$0")/.."

cc -O2 -Wall -Wextra -I. -o /tmp/crossfade_test \
   tests/crossfade_test.c src/system/audio/crossfade.c -lm
/tmp/crossfade_test

python3 tests/check_crossfade_language_keys.py
