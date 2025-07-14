# StructZone

Fuzzing project that attempts to detect struct overflows.

## Quick start

```bash
# the git error is no problem, make check should still work)
docker compose run --build dev_cont 
# In the docker environment, in the top level directory:
make check # runs tests
make bench # runs benchmark
```

## File structure

* The `benchmark` directory contains the self-created linked-list benchmark with runner and a
 plotter scripts.
* The `docker` directory contains the `Dockerfile` and an entrypoint script that automatically
 compiles the project on docker entry.
* The `llvm-pass` directory contains the files for the sanitizer pass.
* The `runtime` directory contains the files for the AVL tree runtime library.
* The `test` directory contains testing infrastructure. The llvm-in subfolder contains files before
 our pass ran, the llvm-out directory contains files after our pass ran.

## Dependencies & Environment

We provide the environment using docker (compose). You can enter the environment with:

```bash
# the git error is no problem, make check should still work)
docker compose run --build dev_cont 
```

If you prefer your own environment, look into the `docker/Dockerfile` for what packages are
 installed for this project to setup your own environment with.

## Running

### Tests

There's a directory `test` which contains toy examples that showcase different types of usages of
 structs (and overflows on them). To run them, simply run `make check` in the top level directory of
 the project. If you alter the sanitizer, runtime or another component, this should automatically
 trigger a rebuild, but if you want to be sure, you can manually clean intermediate files with
 `make clean`.

### Benchmark

To profile our code, we created a benchmark that uses linked-lists, as this is a struct-heavy use
 scenario. To run them, simply run `make bench` in the top level directory of the project.

## Commits

When commiting, some pre-commit formatting is done to ensure consistent style in files. To set this
 up outside of docker, install [pre-commit](https://pre-commit.com/) and run pre-commit install.
 This, however, stops the GUI versions of working (usually), so either stick to the CLI git, or
 check the linked repo to see their work around.
