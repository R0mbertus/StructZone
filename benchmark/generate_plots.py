#!/usr/bin/env python3
import matplotlib.pyplot as plt
import numpy as np

def chunk(inp, n):
    return [inp[i:i + n] for i in range(0, len(inp), n)]
    
versioning_info = []
size_info = []
time_mem_info = []
with open("benchmark_out", "r") as bench_file:
    run_contents = bench_file.readlines()
    
if run_contents[0] != "===========versioning===========\n":
    raise Exception("Unexpected benchmark file format!")

header_count = 1

# Get out the different contents of the benchmark result
for line in run_contents[1:]:
    if line == "==========binary sizes==========\n" or \
       line == "================================\n":
        header_count += 1
    elif header_count == 1:
        versioning_info.append(line)
    elif header_count == 2:
        size_info.append(line)
    else:
        time_mem_info.append(line)

# Parse binary size measurements.
bin_size_x = []
bin_size_y = []
for size_chunk in chunk(size_info, 4):
    if len (size_chunk) == 4:
        bin_size_x.append(int(size_chunk[1].lstrip("orig size: ")))
        bin_size_y.append(int(size_chunk[2].lstrip("new size: ")))
    else:
        bin_size_x.append(int(size_chunk[0].lstrip("orig benchmark: ")))
        bin_size_y.append(int(size_chunk[1].lstrip("new benchmark: ")))

# Plot the binary size measurements on a scatter plot, with trend line
fig_bin_size, ax_bin_size = plt.subplots()
ax_bin_size.scatter(bin_size_x, bin_size_y)
z = np.polyfit(bin_size_x, bin_size_y, 1)
p = np.poly1d(z)

ax_bin_size.plot(bin_size_x,p(bin_size_x),"r")
ax_bin_size.set(xlabel="original binary size (bytes)", ylabel="sanitized binary size (bytes)",
    title = "Plot of binary size overhead")
fig_bin_size.savefig("bin_size.pdf")

plt.figure(figsize=(5,5))
plt.plot(bin_size_x,p(bin_size_x),"r")
plt.xlabel(xlabel="original binary size (bytes)", fontsize=13)
plt.ylabel(ylabel="sanitized binary size (bytes)", fontsize=13)
plt.xticks(fontsize=13)
plt.yticks(fontsize=13)
plt.title("Plot of binary size overhead", fontsize = 18)
plt.savefig("bin_size.pdf", bbox_inches="tight")
plt.savefig("bin_size.png", bbox_inches="tight")

run_sizes_x = []
time_orig_y = []
time_orig_stdev = []
time_new_y = []
time_new_stdev = []

mem_orig_y = []
mem_orig_stdev = []
mem_new_y = []
mem_new_stdev = []
# Separate, then parse time and memory measurements.
for time_mem_chunk in chunk(time_mem_info, 7):
    run_sizes_x.append(int(time_mem_chunk[0].lstrip("run size: ")))
    time_orig_y.append(float(time_mem_chunk[1].split(" (stdev ")[0].lstrip("original mean: ")))
    time_orig_stdev.append(float(time_mem_chunk[1].split(" (stdev ")[1].rstrip(")\n")))
    time_new_y.append(float(time_mem_chunk[2].split(" (stdev ")[0].lstrip("new mean: ")))
    time_new_stdev.append(float(time_mem_chunk[2].split(" (stdev ")[1].rstrip(")\n")))
    mem_orig_y.append(float(time_mem_chunk[4].split(" (stdev ")[0].lstrip("peak mem usage original: ")))
    mem_orig_stdev.append(float(time_mem_chunk[4].split(" (stdev ")[1].rstrip(")\n")))
    mem_new_y.append(float(time_mem_chunk[5].split(" (stdev ")[0].lstrip("new peak mem usage: ")))
    mem_new_stdev.append(float(time_mem_chunk[5].split(" (stdev ")[1].rstrip(")\n")))

# Plot the time measurements as a simple plot with error bars.
fig_time, ax_time = plt.subplots()
ax_time.errorbar(run_sizes_x, time_orig_y, yerr=time_orig_stdev, ecolor="red", label="Unsanitized")
ax_time.errorbar(run_sizes_x, time_new_y, yerr=time_new_stdev, ecolor="red", label="Sanitized")
ax_time.set(xlabel="Run size (no. of structs)", ylabel="execution time (ms)",
    title = "Plot of temporal overhead")
ax_time.legend()
fig_time.savefig("time.pdf")

plt.figure(figsize=(5,5))
plt.errorbar(run_sizes_x, time_orig_y, yerr=time_orig_stdev, ecolor="red", label="Unsanitized")
plt.errorbar(run_sizes_x, time_new_y, yerr=time_new_stdev, ecolor="red", label="Sanitized")
plt.xlabel(xlabel="Run size (no. of structs)", fontsize=13)
plt.ylabel(ylabel="execution time (ms)",fontsize=13)
plt.xticks(fontsize=13)
plt.yticks(fontsize=13)
plt.title("Plot of temporal overhead",fontsize=18)
plt.savefig("time.pdf",bbox_inches='tight')
plt.savefig("time.png",bbox_inches='tight')

# Plot the space measurements as a simple plot with error bars.
fig_space, ax_space = plt.subplots()
ax_space.errorbar(run_sizes_x, mem_orig_y, yerr=mem_orig_stdev, ecolor="red", label="Unsanitized")
ax_space.errorbar(run_sizes_x, mem_new_y, yerr=mem_new_stdev, ecolor="red", label="Sanitized")
ax_space.set(xlabel="Run size (no. of structs)", ylabel="peak memory use (bytes)",
    title = "Plot of spatial overhead")
ax_space.legend()
fig_space.savefig("space.pdf")	

plt.figure(figsize=(5,5))
plt.errorbar(run_sizes_x, mem_orig_y, yerr=mem_orig_stdev, ecolor="red", label="Unsanitized")
plt.errorbar(run_sizes_x, mem_new_y, yerr=mem_new_stdev, ecolor="red", label="Sanitized")
plt.xlabel(xlabel="Run size (no. of structs)", fontsize=13)
plt.ylabel(ylabel="peak memory use (bytes)",fontsize=13)
plt.xticks(fontsize=13)
plt.yticks(fontsize=13)
plt.title("Plot of spatial overhead",fontsize=18)
plt.savefig("space.pdf",bbox_inches='tight')
plt.savefig("space.png",bbox_inches='tight')