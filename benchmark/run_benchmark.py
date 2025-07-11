#! /usr/bin/python3
import os
import glob
import time
import random
import subprocess as sp
import sys
import psutil
from statistics import stdev, fmean

def single_run(benchmark_prog: str, seed: int, size: int)-> [str, float, int]:
    # First run the benchmark and measure the time.
    start_time = time.time()
    res = sp.run([benchmark_prog, str(seed), str(size)], capture_output=True, text=True)
    end_time = time.time()
    # Next, run it with piping so we can measure the memory usage.
    proc = sp.Popen([benchmark_prog, str(seed), str(size)], stdout=sp.PIPE, stderr=sp.PIPE, text=True)
    ps_proc = psutil.Process(proc.pid)
    peak_mem: int = 0
    while proc.poll() is None:
        try:
            mem = ps_proc.memory_info().rss
            peak_mem = max(peak_mem, mem)
        except psutil.NoSuchProcess:
            break
        time.sleep(0.01)
    stdout, stderr = proc.communicate()
    assert(proc.returncode == 0)
    assert(res.returncode == 0)
    assert(res.stdout == stdout)
    time_taken = end_time-start_time
    return (stdout, time_taken, peak_mem)
    
def dual_run(seed: int, size: int) -> [float, float, int, int]:
    (res_orig, time_orig, mem_orig) = single_run("bin/benchmark.orig", seed, size);
    (res, time, mem) = single_run("bin/benchmark", seed, size);
    assert(res_orig == res)
    return (time_orig, time, mem_orig, mem)

def dual_run_rep(size: int, repetitions: int) -> [float, float, float, float]:
    orig_times: [int] = []
    times: [int] = []
    mems: [int] = []
    orig_mems: [int] = []
    for i in range(0, repetitions):
        seed: int = random.randint(0, 2147483647)
        (time_orig, time, mem_orig, mem) = dual_run(seed, size)
        orig_times.append(time_orig)
        times.append(time)
        mems.append(mem)
        orig_mems.append(mem_orig)
    mean_orig: float = fmean(orig_times)
    stdev_orig: float = stdev(orig_times)
    mean_new: float = fmean(times)
    stdev_new: float = stdev(times)
    mem_orig: float = fmean(orig_mems)
    stdev_orig_mem: float = stdev(orig_mems)
    mem_new: float = fmean(mems)
    stdev_mem_new: float = stdev(mems)
    return (mean_orig, stdev_orig, mean_new, stdev_new, mem_orig, stdev_orig_mem, mem_new, stdev_mem_new)

def full_run(size: int):
    (mean_orig, stdev_orig, mean_new, stdev_new, mem_orig, stdev_orig_mem, mem_new, stdev_mem_new) = dual_run_rep(size, 1000)
    print(f"run size: {size}")
    print(f"original mean: {mean_orig} (stdev {stdev_orig})")
    print(f"new mean: {mean_new} (stdev {stdev_new}))")
    print(f"time overhead: {mean_new/mean_orig}")
    print(f"peak mem usage original: {mem_orig} (stdev {stdev_orig_mem})")
    print(f"new peak mem usage: {mem_new} (stdev {stdev_mem_new})")
    print(f"space overhead: {mem_new/mem_orig}")
    print("="*32)

orig_benchmark_size = os.path.getsize("bin/benchmark.orig")
new_benchmark_size = os.path.getsize("bin/benchmark")
print("="*11 + "versioning" + "="*11)
print("Python version:")
print(sys.version)
print('\n')
opt_inf = sp.run(["opt", "--version"], capture_output=True, text=True)
print("LLVM version:")
print(opt_inf.stdout)
print('\n')
gcc_inf = sp.run(["gcc", "--version"], capture_output=True, text=True)
print("GCC version:")
print(gcc_inf.stdout)
print('\n')
make_inf = sp.run(["make", "--version"], capture_output=True, text=True)
print("Make version:")
print(make_inf.stdout)
print(f"="*10+"binary sizes"+"="*10)
for orig_file in glob.glob("../test/bin/*.orig"):
    orig_size = os.path.getsize(orig_file)
    new_size = os.path.getsize(orig_file.replace(".orig", ""))
    print(f"for file: {orig_file.split('/')[-1]}")
    print(f"orig size: {orig_size}")
    print(f"new size: {new_size}")
    print(f"overhead: {new_size/orig_size}")
print(f"orig benchmark: {orig_benchmark_size}")
print(f"new benchmark: {new_benchmark_size}")
print(f"overhead: {new_benchmark_size/orig_benchmark_size}")
print("="*32)
full_run(10)
full_run(100)
full_run(1000)
full_run(10000)
full_run(100000)
