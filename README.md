# StructZone

Fuzzing project that attempts to detect struct overflows.

## Dependencies & Environment

For the best experience, run the docker container using `docker compose up`. If you instead want to
 run the project locally, install the dependencies listed in the [Dockerfile](./docker/Dockerfile). 

### Commits

When commiting, a `clang-format` hook runs, as taken from [here](https://github.com/barisione/clang-format-hooks).
 This, however, stops the GUI versions of working (usually), so either stick to the CLI git, or
 check the linked repo to see their work around.

## Running

### Tests

There's a directory `test` which contains toy examples that showcase different types of usages of
 structs (and overflows on them). To run them, run `./compile-test.sh <filename without extension>`.
 TODO: update once testing scripts are complete

### Spec Benchmark

TODO: when spec comes in write this
