#!/bin/bash

pushd /src/src/llvm-pass/
./compile_pass.sh
popd

pushd /src/test
TESTS=$(find . -name '*.c')
for i in ${x%.*}; do # Not recommended, will break on whitespace
    ./compile-test.sh "$i"
done
popd

/bin/bash