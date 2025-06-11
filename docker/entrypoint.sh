#!/bin/bash

pushd /src/llvm-pass/
pwd
make
popd

pushd /src/test

popd

/bin/bash