# tool macros
COMP ?= compile_test.sh

SRC_DIR := src
BIN_DIR := bin
IN_DIR := llvm-in
OUT_DIR := llvm-out

CFILES = $(shell find . -type f -name "*.c" )
TESTS = $(shell for i in $(CFILES); do basename -s .c $$i; done)

RUNTIME_DIR="../runtime/"
RUNTIME_BIN_DIR=$(RUNTIME_DIR)"bin/"
RUNTIME_INC_DIR=$(RUNTIME_DIR)"src/"
RUNTIME_LLVM_DIR=$(RUNTIME_DIR)"llvm/"
PASS_DIR="../llvm-pass"

# default rule
default: all

bench: all
	python3 ./run_benchmark.py
	python3 ./generate_plots.py

all: dirs $(addprefix $(BIN_DIR)/, $(TESTS))
	clang src/benchmark.c -o bin/benchmark.orig

list:
	@echo $(addprefix $(BIN_DIR)/, $(TESTS))

dirs:
	mkdir -p $(BIN_DIR)
	mkdir -p $(IN_DIR)
	mkdir -p $(OUT_DIR)

.PRECIOUS: $(IN_DIR)/%.ll
$(IN_DIR)/%.ll: $(SRC_DIR)/%.c
	clang -I ${RUNTIME_INC_DIR} -g -S -emit-llvm -Xclang -o1 $< -o $@ -L$(RUNTIME_BIN_DIR) -l:Runtime.a
	

.PRECIOUS: $(OUT_DIR)/%.out.ll
$(OUT_DIR)/%.out.ll: $(IN_DIR)/%.ll
	opt -load-pass-plugin=$(PASS_DIR)"/bin/Sanitizer.so" -S -passes="function(mem2reg),structzone-sanitizer" $< -o $@

$(BIN_DIR)/%: $(OUT_DIR)/%.out.ll
	clang -L$(RUNTIME_BIN_DIR) -g $< -o $@ -g -fstandalone-debug -l:Runtime.a -lm -lstdc++ 

.PHONY: clean
clean:
	@echo rm ./**/*.ll ./bin/*
	@rm -f ./**/*.ll ./bin/*
