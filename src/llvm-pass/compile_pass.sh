#! /bin/bash

set -e

g++ $(llvm-config --cxxflags --libfiles all) \
    -shared -fPIC -g -fsanitize=undefined -fno-sanitize-recover=all -Wno-missing-template-keyword \
    Sanitizer.cpp -o Sanitizer.so
