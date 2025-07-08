# StructZone

Fuzzing project that attempts to detect struct overflows.

## Dependencies & Environment

We provide the environment using docker (compose). You can enter the environment with:
```bash
mkdir -p /tmp/spec-2017
SPEC_ROOT=/tmp/spec-2017 docker compose run --build dev_cont
``` 
(the git error is no problem, make check should still work)

To run all the tests, use (after entering the environment):
```bash
make check
```

Additionally, if you want to run the SPEC benchmark. make sure there's an install 
of SPEC CPU2017 present on your system, and set the environment variable 
`$SPEC_ROOT` to the root of the install directory.

### Commits

When commiting, some pre-commit formatting is done to ensure consistent style in files. To set this
 up outside of docker, install [pre-commit](https://pre-commit.com/) and run pre-commit install.
 This, however, stops the GUI versions of working (usually), so either stick to the CLI git, or
 check the linked repo to see their work around.

## Running

### Tests

There's a directory `test` which contains toy examples that showcase different types of usages of
 structs (and overflows on them). To run them, simply run `make check` in the top level directory of the project. If you alter the sanitizer, runtime or another component, this should automatically trigger a rebuild, but if you want to be sure, you can manually clean intermediate files with `make clean`.
 
## File structure:
* The llvm-pass contains the files for the sanitizer pass.
* The runtime folder contains the files for the AVL tree runtime library.
* The test directory contains testing infrastructure. The llvm-in subfolder contains files before our pass ran, the llvm-out directory contains files after our pass ran.

### Spec Benchmark

For the spec benchmark, a selection was made of worloads that are written in C and make usage of
 structs with only simple library calls on the structs. The ones chosen from these constraints are
 `605.mcf_s, TODO: find more`.

There are two configs, `docker/spec-files/base.cfg` or `docker/spec-files/structzone.cfg` that are
 for the base and StructZone enabled runs respectively. Running can be done using the following
 command:

```sh
./docker/spec_setup.sh
cd $SPEC_ROOT && source shrc
runcpu --config=<base/structzone>.cfg structs
```
