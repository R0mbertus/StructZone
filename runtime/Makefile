# tool macros
CC ?= clang	
CXX ?= clang++
CFLAGS := -c -fPIC -g -fno-omit-frame-pointer
CXXFLAGS := -c -fPIC -g -fno-omit-frame-pointer
DBGFLAGS := -g
COBJFLAGS := $(CFLAGS) -c

# path macros
BIN_PATH := bin
OBJ_PATH := obj
SRC_PATH := src
DBG_PATH := debug

all: bin/Runtime.a llvm/Runtime.ll bin/runtimetest

obj/Runtime.o: src/Runtime.cpp src/Runtime.h
	clang++ src/Runtime.cpp $(CXXFLAGS) -o obj/Runtime.o

bin/Runtime.a: obj/Runtime.o
	ar r bin/Runtime.a obj/Runtime.o

llvm/Runtime.ll: src/Runtime.cpp
	clang++ src/Runtime.cpp $(CXXFLAGS) -S -emit-llvm
	mv ./Runtime.ll ./llvm/Runtime.ll

bin/runtimetest: src/RuntimeTest.cpp bin/Runtime.a
	clang++ src/RuntimeTest.cpp -g -fno-omit-frame-pointer -o ./bin/runtimetest -L./bin -l:Runtime.a -fsanitize=address

clean:
	rm -f bin/*
	rm -f llvm/*
	rm -f obj/*