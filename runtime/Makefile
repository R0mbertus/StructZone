# tool macros
CC ?= clang	
CXX ?= clang++
CFLAGS := -shared -fPIC -g
CXXFLAGS := -shared -fPIC -g
DBGFLAGS := -g
COBJFLAGS := $(CFLAGS) -c

# path macros
BIN_PATH := bin
OBJ_PATH := obj
SRC_PATH := src
DBG_PATH := debug

all: bin/libruntime.so llvm/Runtime.ll

bin/libruntime.so: src/Runtime.cpp
	clang++ src/Runtime.cpp $(CXXFLAGS) -o bin/libruntime.so

llvm/Runtime.ll: src/Runtime.cpp
	clang++ src/Runtime.cpp $(CXXFLAGS) -S -emit-llvm
	mv ./Runtime.ll ./llvm/Runtime.ll

clean:
	rm bin/*
	rm llvm/*