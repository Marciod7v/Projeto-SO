#!/bin/sh
set -eu

if [ ! -f nested/level1/level2/payload.txt ] ||
   [ "$(cat nested/level1/level2/payload.txt)" != "recursive copy fixture" ]; then
  : "${CLOUDIST_SIMPLE_INPUT_ERROR:?error marker path is not set}"
  printf '%s\n' "$(pwd -P)" > "$CLOUDIST_SIMPLE_INPUT_ERROR"
  exit 1
fi

: "${CLOUDIST_SIMPLE_CWD_MARKER:?marker path is not set}"
: > "$CLOUDIST_SIMPLE_CWD_MARKER"
exec sleep 2
