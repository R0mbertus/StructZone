default: all

check: all
	make -C ./test check

all: runtime_makage llvm-pass_makage test_makage

runtime_makage:
	make -C ./runtime

llvm-pass_makage:
	make -C ./llvm-pass

test_makage: llvm-pass_makage runtime_makage
	make -C ./test

clean:
	make -C ./runtime clean
	make -C ./test clean
	make -C ./llvm-pass clean