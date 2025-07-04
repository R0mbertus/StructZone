#! /usr/bin/python3
import time
import random
import subprocess as sp
from statistics import stdev, fmean

def single_run(benchmark_prog: str, seed: int, size: int)-> [str, float]:
    start_time = time.time()
    res = sp.run([benchmark_prog, str(seed), str(size)], capture_output=True, text=True)
    end_time = time.time()
    time_taken = end_time-start_time
    assert(res.returncode == 0)
    return (res.stdout, time_taken)

def dual_run(seed: int, size: int) -> [float, float]:
    (res_orig, time_orig) = single_run("bin/benchmark.orig", seed, size);
    (res, time) = single_run("bin/benchmark", seed, size);
    assert(res_orig == res)
    return (time_orig, time)

def dual_run_rep(size: int, repetitions: int) -> [float, float, float, float]:
    orig_times: [int] = []
    times: [int] = []
    for i in range(0, repetitions):
        seed: int = random.randint(0, 2147483647)
        (time_orig, time) = dual_run(seed, size)
        orig_times.append(time_orig)
        times.append(time)
    mean_orig: float = fmean(orig_times)
    stdev_orig: float = stdev(orig_times)
    mean_new: float = fmean(times)
    stdev_new: float = stdev(times)
    return (mean_orig, stdev_orig, mean_new, stdev_new)

def full_run(size: int):
    (mean_orig, stdev_orig, mean_new, stdev_new) = dual_run_rep(size, 1000)
    print(f"run size: {size}")
    print(f"original mean: {mean_orig}")
    print(f"standard dev: {stdev_orig}")
    print(f"new mean: {mean_new}")
    print(f"new standard dev: {stdev_new}")
    print(f"diff: {mean_new/mean_orig}")
    print("="*32)

full_run(10)
full_run(100)
full_run(1000)
full_run(10000)
full_run(100000)
