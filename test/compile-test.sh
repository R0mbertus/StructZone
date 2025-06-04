#! /bin/bash

set -e

if [ -z "$1" ]
then
    echo "Usage: $0 <filename without extension>"
    exit 1
fi

pass_dir="../llvm-pass"
# Check if the sanitizer has already been built, build if not. Yes, I'm aware, you could use makefiles for this but i can't be arsed right now.
if [ ! -e $pass_dir"/bin/Sanitizer.so" ]
then
    make -C $pass_dir
fi
# Then get the llvm IR out of the requested input file.
clang -S -emit-llvm "$1.c"

# Run the optimization pass.
opt -load-pass-plugin=$pass_dir"/bin/Sanitizer.so" -S -passes=structzone-sanitizer $1".ll" -o $1".out.ll"
# Since this is for testing purposes, we aren't going to remove the IR files.

# And then compile that llvm IR with the sanitation pass enabled.
clang $1".out.ll" -o $1 -g -fstandalone-debug
