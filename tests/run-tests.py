#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Henrique Bucher
"""Validate the loop-hunter: every `irr_*` function must be flagged as
irreducible, every `red_*` function must NOT be. Exit nonzero on any mismatch.

The point: a tool that reports "0 irreducible" on a clean codebase is only
trustworthy if we've proven it flags real positives AND ignores real negatives.
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PLUGIN = os.path.join(ROOT, 'libLoopHunter.so')
CLANG = os.environ.get('CLANG', 'clang++-20')
OPT = os.environ.get('OPT', 'opt-20')

def defined_functions(ll_path):
    """All extern-C test functions defined in the IR (unmangled @irr_/@red_)."""
    names = set()
    for line in open(ll_path):
        m = re.match(r'define\b.*@((?:irr|red)_[A-Za-z0-9_]+)\s*\(', line)
        if m:
            names.add(m.group(1))
    return names

def flagged_functions(ll_path):
    r = subprocess.run(
        [OPT, f'-load-pass-plugin={PLUGIN}', '-passes=function(loop-hunter)',
         '-disable-output', ll_path],
        capture_output=True, text=True)
    flagged = set()
    for line in r.stderr.splitlines():
        m = re.search(r'warning: irreducible loop in (\S+)', line)
        if m:
            flagged.add(m.group(1))
    return flagged, r.stderr

def run_file(src, opt_level):
    ll = f'/tmp/lh_test_{os.path.basename(src)}.{opt_level.strip("-")}.ll'
    c = subprocess.run(
        [CLANG, opt_level, '-g', '-std=c++20', '-S', '-emit-llvm', src, '-o', ll],
        capture_output=True, text=True)
    if c.returncode != 0:
        print(f"  COMPILE FAILED: {src}\n{c.stderr}")
        return None
    defined = defined_functions(ll)
    flagged, raw = flagged_functions(ll)
    return defined, flagged, raw

def main():
    if not os.path.exists(PLUGIN):
        print(f"missing plugin {PLUGIN}; build it first"); sys.exit(2)

    files = [os.path.join(HERE, 'positive_irreducible.cpp'),
             os.path.join(HERE, 'negative_reducible.cpp')]
    # Irreducibility can be created by higher opt levels (jump threading), so
    # check across opt levels: a function must be classified consistently.
    opt_levels = ['-O0', '-O1', '-O2', '-O3']

    total, passed = 0, 0
    failures = []
    for src in files:
        print(f"\n=== {os.path.basename(src)} ===")
        for lvl in opt_levels:
            res = run_file(src, lvl)
            if res is None:
                failures.append((src, lvl, 'compile-failed')); continue
            defined, flagged, raw = res
            for fn in sorted(defined):
                total += 1
                should_flag = fn.startswith('irr_')
                is_flagged = fn in flagged
                ok = (should_flag == is_flagged)
                passed += ok
                status = 'PASS' if ok else 'FAIL'
                if not ok:
                    failures.append((fn, lvl,
                        f"expected {'flag' if should_flag else 'no-flag'}, "
                        f"got {'flag' if is_flagged else 'no-flag'}"))
                    print(f"  [{status}] {lvl:4} {fn}  <-- MISMATCH")
                else:
                    print(f"  [{status}] {lvl:4} {fn}")

    print("\n" + "=" * 60)
    print(f"{passed}/{total} checks passed across {len(opt_levels)} opt levels")
    if failures:
        print(f"{len(failures)} FAILURE(S):")
        for f in failures:
            print("  -", f)
        sys.exit(1)
    print("ALL GREEN: hunter flags every irreducible loop and no reducible one.")

if __name__ == '__main__':
    main()
