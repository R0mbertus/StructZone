#!/bin/bash

pushd /src/llvm-pass/
make
popd

pushd /src/
pre-commit install
popd

/bin/bash
