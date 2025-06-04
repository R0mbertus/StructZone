FUZZER_NAME := 'fuzzbench'
PROJECT_DIR := absolute_path(".")
PROFILE := env("PROFILE", "release")
PROFILE_DIR := if PROFILE == "release" { "release" } else if PROFILE == "dev" { "debug" } else { "debug" }
CARGO_TARGET_DIR := env("CARGO_TARGET_DIR", "target")
FUZZER := CARGO_TARGET_DIR / PROFILE_DIR / FUZZER_NAME


alias build := fuzzer
alias cc := cxx

[linux]
[macos]
cxx:
	cargo build --profile={{PROFILE}}

fuzz_o fuzz_source:
	{{CARGO_TARGET_DIR}}/{{PROFILE_DIR}}/libafl_cc --libafl-no-link -O3 -c {{fuzz_source}}.c -o {{fuzz_source}}.o

fuzzer fuzz_source: cxx (fuzz_o fuzz_source)
	{{CARGO_TARGET_DIR}}/{{PROFILE_DIR}}/libafl_cxx --libafl {{fuzz_source}}.o -o {{FUZZER_NAME}} -lm -lz

run fuzz_source: cxx (fuzz_o fuzz_source) (fuzzer fuzz_source)
	#!/bin/bash
	rm -rf libafl_unix_shmem_server || true
	mkdir in || true
	echo a > in/a
	./{{FUZZER_NAME}} -o out -i in
	# RUST_LOG=info ./{{FUZZER_NAME}} -o out -i seed

clean:
	cargo clean
