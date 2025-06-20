#! /bin/bash

runtime_dir="../runtime/"
runtime_dir_bin=$runtime_dir"bin/"
runtime_llvm_dir=$runtime_dir"llvm/"
pass_dir="../llvm-pass/"

set -e

if [ -z "$1" ]
then
    echo "Usage: $0 <filename without extension>"
    exit 1
fi

# Always run make on the pass to ensure it's up to date.
make -C $pass_dir
make -C $runtime_dir

# Then get the llvm IR out of the requested input file.
clang -S -emit-llvm "$1.c"

set -x
# llvm-link -S ./$1.ll ./$runtime_llvm_dir"Runtime.ll" -o ./$1.runtimed.ll

# Run the optimization pass.
opt -load-pass-plugin=$pass_dir"/bin/Sanitizer.so" -S -passes=structzone-sanitizer $1".ll" -o $1".out.ll"
# Since this is for testing purposes, we aren't going to remove the IR files.

# And then compile that llvm IR with the sanitation pass enabled.
clang $1".out.ll" -o $1 -g -fstandalone-debug -L$runtime_dir_bin -l:Runtime.a -v -lm -lstdc++
