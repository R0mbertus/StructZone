#!/usr/bin/env python3
import os
import subprocess as sp

COLORS = {
    "KNRM":"\x1B[0m",
    "KRED":"\x1B[31m",
    "KGRN":"\x1B[32m",
    "KYEL":"\x1B[33m",
    "KBLU":"\x1B[34m",
    "KMAG":"\x1B[35m",
    "KCYN":"\x1B[36m",
    "KWHT":"\x1B[37m"
}

FAILING_TESTS = [
    "toy.arr.external_overflow",
    "toy.heap.arr.external_overflow",
    "toy.heap.internal_overflow",
    "toy.internal_overflow",
    "toy.nested.internal_overflow",
]

SUCCEEDING_TESTS = [
    "toy.arr.safe",
    "toy.heap.arr.safe",
    "toy.heap.safe",
    "toy.nested.safe",
    "toy.safe",
]

def run_test():
    for i in FAILING_TESTS + SUCCEEDING_TESTS:
        exited_normally = sp.run([f"./bin/{i}"], stdout= sp.DEVNULL, stderr=sp.DEVNULL).returncode == 0
        expected_succ = i in SUCCEEDING_TESTS
        
        if(exited_normally ^ expected_succ):
            print(f"{COLORS["KRED"]}[FAILED]{COLORS["KNRM"]} {i}")
        else:
            print(f"{COLORS["KGRN"]}[PASSED]{COLORS["KNRM"]} {i}")
        

if __name__ == "__main__":
    abspath = os.path.abspath(__file__)
    dname = os.path.dirname(abspath)
    os.chdir(dname)

    run_test()