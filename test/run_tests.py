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
        res = sp.run([f"./bin/{i}"], capture_output = True, text = True)
        exited_normally = res.returncode == 0
        expected_succ = i in SUCCEEDING_TESTS
        
        if exited_normally ^ expected_succ:
            print(f"{COLORS['KRED'] }[FAILED]{COLORS['KNRM']} {i}")
            continue;
        
        if expected_succ:
            # Compile the source file with gcc, so that we can check against its unaltered output.
            sp.check_call(["gcc", f"./src/{i}.c", "-o", f"./bin/{i}.orig"])
            orig_res = sp.run([f"./bin/{i}.orig"], capture_output = True, text = True)
            if orig_res.stdout != res.stdout:
                print(f"{COLORS['KRED'] }[FAILED]{COLORS['KNRM']} {i}")
                print("Exit code is correct, but program behaviour has been altered!")
                print(f"Expected: \n{orig_res.stdout}\nBut got:\n{res.stdout}")
                continue
        elif "ILLEGAL ACCESS AT" not in res.stderr:
            	print(f"{COLORS['KRED'] }[FAILED]{COLORS['KNRM']} {i}")
            	print(f"Expected stderr to contain the sanitizer error message, but this did not happen.")
            	continue
        print(f"{COLORS['KGRN']}[PASSED]{COLORS['KNRM']} {i}")
        

if __name__ == "__main__":
    abspath = os.path.abspath(__file__)
    dname = os.path.dirname(abspath)
    os.chdir(dname)

    run_test()
