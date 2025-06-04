#!/bin/bash

pushd /src/src/llvm-pass/
./compile_pass.sh
popd

pushd /src/test
TESTS=$(find . -name '*.c')
echo $TESTS
echo $y
for i in $TESTS; do # Not recommended, will break on whitespace
    y=${i%.c}
    x=${y##*/}
    ./compile-test.sh "$x"
done
popd

/bin/bash