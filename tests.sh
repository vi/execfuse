#!/bin/bash

set -e
set -x

mkdir -p m
./execfuse examples/xmp m
trap 'fusermount -u m' EXIT


test -x m/`pwd`/execfuse

test "$(find  m/`pwd` -maxdepth 1  -name '*.c' -printf '%s %f\n')" == \
     "$(find  .       -maxdepth 1  -name '*.c' -printf '%s %f\n')"

cmp m/`pwd`/execfuse ./execfuse
