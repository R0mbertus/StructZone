#! /bin/bash

pass_dir="../llvm-pass/bin/"
runtime_dir="../runtime/bin"
runtime_llvm_dir="../runtime/llvm/"
# Check if the sanitizer has already been built, build if not. Yes, I'm aware, you could use makefiles for this but i can't be arsed right now.
if [ ! -e $pass_dir"Sanitizer.so" ]
then
    dir=$(pwd)
    cd $pass_dir
    source compile_pass.sh
    cd $dir
fi
# Then get the llvm IR out of the requested input file.
clang -S -emit-llvm "$1.c"

set -x
llvm-link -S ./$1.ll ./$runtime_dir"Runtime.ll" -o ./$1.runtimed.ll

# Run the optimization pass.
opt -load-pass-plugin=$pass_dir"Sanitizer.so" -S -passes=structzone-sanitizer $1".runtimed.ll" -o $1".out.ll"
# Since this is for testing purposes, we aren't going to remove the IR files.

# And then compile that llvm IR with the sanitation pass enabled.
clang $1".out.ll" -o $1 -g -fstandalone-debug -L$runtime_dir -l:Runtime.a -v
