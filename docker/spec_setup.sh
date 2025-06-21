#!/bin/bash

set -e

if [ -z "$SPEC_ROOT" ]; then
  echo "SPEC_ROOT is not set. Please set it to the root of the SPEC CPU benchmark suite."
  exit 1
fi

cd $(dirname "$0")
cp spec-files/*.cfg "$SPEC_ROOT/config/"
cp spec-files/*.bset "$SPEC_ROOT/benchspec/CPU/"
cp spec-files/Makefile.defaults "$SPEC_ROOT/benchspec/Makefile.defaults"

export SANITIZER_PASS="$(pwd)/../llvm-pass/bin/Sanitizer.so"
export RUNTIME_LIB="$(pwd)/../runtime/bin/"

echo "Run 'cd $SPEC_ROOT && source shrc' to set up the environment for SPEC CPU."
echo "Then run with runcpu --config=<base/structzone> structs"
