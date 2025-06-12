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
RUNTIME_LLVM_DIR=$(RUNTIME_DIR)"llvm/"
PASS_DIR="../llvm-pass"

# default rule
default: dirs $(addprefix $(BIN_DIR)/, $(TESTS))

list:
	@echo $(addprefix $(BIN_DIR)/, $(TESTS))

dirs:
	mkdir -p $(BIN_DIR)
	mkdir -p $(IN_DIR)
	mkdir -p $(OUT_DIR)

$(IN_DIR)/%.ll: $(SRC_DIR)/%.c
	clang -S -emit-llvm $< -o $@

$(OUT_DIR)/%.out.ll: $(IN_DIR)/%.ll
	opt -load-pass-plugin=$(PASS_DIR)"/bin/Sanitizer.so" -S -passes=structzone-sanitizer $< -o $@

$(BIN_DIR)/%: $(OUT_DIR)/%.out.ll
	clang $< -o $@ -g -fstandalone-debug -L$(RUNTIME_BIN_DIR) -l:Runtime.a -v -lm -lstdc++


.PHONY: all
all: $(TARGET)

.PHONY: clean
clean:
	@echo rm *.ll
	@rm -f *.ll