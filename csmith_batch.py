#!/usr/bin/env python3
"""b1cc × csmith differential test harness (unbuffered)"""
import subprocess, sys, os, tempfile, time

CSMITH_INC = os.environ.get("CSMITH_PATH",
    "/opt/homebrew/Cellar/csmith/2.3.0/include/csmith-2.3.0")
CSMITH_FLAGS = [
    "--no-longlong", "--no-float", "--no-consts", "--no-volatiles",
    "--no-jumps", "--no-bitfields", "--no-packed-struct", "--no-builtins",
    "--no-divs", "--no-comma-operators",
    "--max-funcs", "2", "--max-block-depth", "2", "--max-block-size", "2",
    "--max-expr-complexity", "4", "--max-array-dim", "1",
    "--max-array-len-per-dim", "3",
    "--max-pointer-depth", "1", "--max-struct-fields", "2",
    "--max-union-fields", "1",
]

COUNT = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
OFFSET = int(sys.argv[2]) if len(sys.argv) > 2 else 100

tmp = f"{tempfile.gettempdir()}/b1cc-csmith-py"
os.makedirs(tmp, exist_ok=True)

pass_, fail, noc = 0, 0, 0
_t0 = time.time()

for i in range(1, COUNT + 1):
    seed = OFFSET + i
    src = f"{tmp}/t{seed}.c"
    ref_bin = f"{tmp}/t{seed}_ref"
    b1cc_asm = f"{tmp}/t{seed}_b1.s"

    # Generate with csmith
    r = subprocess.run(["csmith"] + CSMITH_FLAGS + ["--seed", str(seed), "-o", src],
                       capture_output=True, timeout=30)
    if r.returncode != 0:
        print(f"NOC seed={seed}: csmith failed")
        sys.stdout.flush()
        noc += 1
        continue

    # Reference: compile with host cc
    r = subprocess.run(["cc", f"-I{CSMITH_INC}", src, "-o", ref_bin],
                       capture_output=True, timeout=30)
    if r.returncode != 0:
        print(f"NOC seed={seed}: reference cc failed")
        sys.stdout.flush()
        noc += 1
        continue

    try:
        r = subprocess.run([ref_bin], capture_output=True, timeout=5)
        ref_out = r.stdout.decode()
    except subprocess.TimeoutExpired:
        print(f"NOC seed={seed}: ref timeout")
        sys.stdout.flush()
        noc += 1
        continue
    ref_checksum = [l for l in ref_out.split("\n") if "checksum" in l]
    ref_cs = ref_checksum[0].strip() if ref_checksum else "NOCHECKSUM"

    # b1cc: compile to assembly
    r = subprocess.run(["./build/b1cc", f"-I{CSMITH_INC}", src, "-S", "-o", b1cc_asm],
                       capture_output=True, timeout=15)
    if r.returncode != 0:
        print(f"NOC seed={seed}: b1cc cannot compile")
        sys.stdout.flush()
        noc += 1
        continue

    # Assemble with host cc
    r = subprocess.run(["cc", b1cc_asm, "-o", f"{tmp}/t{seed}_b1"],
                       capture_output=True, timeout=30)
    if r.returncode != 0:
        print(f"NOC seed={seed}: assembly failed")
        sys.stdout.flush()
        noc += 1
        continue

    try:
        r = subprocess.run([f"{tmp}/t{seed}_b1"], capture_output=True, timeout=5)
        b1_out = r.stdout.decode()
    except subprocess.TimeoutExpired:
        print(f"NOC seed={seed}: b1 execution timeout")
        sys.stdout.flush()
        noc += 1
        continue
    b1_checksum = [l for l in b1_out.split("\n") if "checksum" in l]
    b1_cs = b1_checksum[0].strip() if b1_checksum else "NOCHECKSUM"

    if ref_cs == b1_cs:
        print(f"PASS seed={seed} {ref_cs}")
        pass_ += 1
    else:
        print(f"FAIL seed={seed} ref={ref_cs} b1cc={b1_cs}")
        fail += 1
    sys.stdout.flush()

    if i % 50 == 0:
        elapsed = time.time() - _t0
        print(f"  [{i}/{COUNT}] PASS={pass_} FAIL={fail} NOC={noc} "
              f"({elapsed:.0f}s)")
        sys.stdout.flush()

print(f"\n=== FINAL: PASS={pass_} FAIL={fail} NOC={noc} "
      f"out of {COUNT} ===")
