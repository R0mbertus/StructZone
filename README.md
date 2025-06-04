# StructZone

Fuzzing project that attempts to detect struct overflows.

## Building

### Manual

First, install [LibAFLs dependencies](https://github.com/AFLplusplus/LibAFL/tree/main?tab=readme-ov-file#building-and-installing),
 and then build and install the LibAFL provided in [lib/LibAFl](./lib/LibAFL/) using cargo:

```sh
cargo build --release --manifest-path=lib/LibAFL/Cargo.toml
```

Then, to build and run the project `rust`, `llvm`, and `just` need to be installed. Then, simply
 just run `just run <target>`.

## Notes

The current fuzzer is the `fuzzbench` fuzzer found in [fuzzers/inproccess/fuzzbench](./lib/LibAFL/fuzzers/inprocess/fuzzbench/).
 For details about the workings of that fuzzer see the readme there.
