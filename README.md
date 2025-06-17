# StructZone

Fuzzing project that attempts to detect struct overflows.

## Dependencies & Environment

For the best experience, run the docker container using `podman/docker compose up`. If you instead
 want to run the project locally, install the dependencies listed in the [Dockerfile](./docker/Dockerfile).

Additionally, make sure there's an install of SPEC CPU2017 present on your system, and set the
 environment variable `$SPEC_ROOT` to the root of the install directory.

### Commits

When commiting, some pre-commit formatting is done to ensure consistent style in files. To set this
 up outside of docker, install [pre-commit](https://pre-commit.com/) and run pre-commit install.
 This, however, stops the GUI versions of working (usually), so either stick to the CLI git, or
 check the linked repo to see their work around.

## Running

### Tests

There's a directory `test` which contains toy examples that showcase different types of usages of
 structs (and overflows on them). To run them, run `./compile-test.sh <filename without extension>`.
 TODO: update once testing scripts are complete

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
