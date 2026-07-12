#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Henrique Bucher
"""Run the loop-hunter pass across a whole codebase via its compile_commands.json.

For each translation unit it re-issues the compile with clang (emitting LLVM IR
at -O2 -g), then runs the loop-hunter plugin and tallies irreducible loops.

Usage:
  scan-codebase.py <compile_commands.json> [--plugin PATH] [--clang clang++-20]
                   [--opt opt-20] [-O2] [-j N] [--limit N]
"""
import argparse, json, os, shlex, subprocess, sys, tempfile, hashlib
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))

def build_ir_cmd(entry, clang, opt_level, outdir):
    cmd = entry.get('command') or ' '.join(entry['arguments'])
    args = shlex.split(cmd)
    args[0] = clang                         # g++-14 -> clang++-20
    out = []
    skip_next = False
    for i, a in enumerate(args[1:], 1):
        if skip_next:
            skip_next = False
            continue
        if a == '-o':                       # drop the object output
            skip_next = True
            continue
        if a == '-c':                       # not compiling to object
            continue
        if a.startswith('-o'):
            continue
        # GCC-only flags clang rejects; drop defensively
        if a in ('-fno-var-tracking-assignments', '-fno-lifetime-dse',
                 '-fno-delete-null-pointer-checks', '-grecord-gcc-switches',
                 '-mno-fma4', '-fno-canonical-system-headers'):
            continue
        out.append(a)
    key = hashlib.sha1(entry['file'].encode()).hexdigest()[:12]
    ll = os.path.join(outdir, key + '.ll')
    return ([clang] + out + ['-emit-llvm', '-S', '-g', opt_level, '-w',
                             '-Qunused-arguments',
                             '-fno-discard-value-names', '-o', ll],
            ll, entry['directory'])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('db')
    ap.add_argument('--plugin', default=os.path.join(HERE, 'libLoopHunter.so'))
    ap.add_argument('--clang', default='clang++-20')
    ap.add_argument('--opt', default='opt-20')
    ap.add_argument('--opt-level', default='-O2')
    ap.add_argument('-j', type=int, default=os.cpu_count())
    ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()

    db = json.load(open(a.db))
    if a.limit:
        db = db[:a.limit]
    outdir = tempfile.mkdtemp(prefix='loophunt_')
    print(f"scanning {len(db)} TUs -> IR in {outdir} ({a.opt_level}, j={a.j})",
          file=sys.stderr)

    compiled, failed = [], 0
    def emit(entry):
        cmd, ll, cwd = build_ir_cmd(entry, a.clang, a.opt_level, outdir)
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
        return (entry['file'], ll) if (r.returncode == 0 and os.path.exists(ll)) else None

    with ThreadPoolExecutor(max_workers=a.j) as ex:
        for res in ex.map(emit, db):
            if res: compiled.append(res)
            else:   failed += 1

    print(f"IR emitted: {len(compiled)} ok, {failed} skipped (clang-incompatible)",
          file=sys.stderr)

    total_irr = 0
    hits = []
    def hunt(item):
        src, ll = item
        r = subprocess.run(
            [a.opt, f'-load-pass-plugin={a.plugin}',
             '-passes=function(loop-hunter)', '-disable-output', ll],
            capture_output=True, text=True)
        return src, r.stderr

    with ThreadPoolExecutor(max_workers=a.j) as ex:
        for src, out in ex.map(hunt, compiled):
            n = out.count('warning: irreducible loop')
            if n:
                total_irr += n
                hits.append((src, out))

    print("\n" + "=" * 70)
    if total_irr == 0:
        print(f"CLEAN: 0 irreducible loops across {len(compiled)} translation units.")
    else:
        for src, out in hits:
            print(f"\n### {src}")
            print(out.rstrip())
        print(f"\nTOTAL: {total_irr} irreducible loop(s) in {len(hits)} file(s) "
              f"of {len(compiled)} scanned.")
    print("=" * 70)
    # Nonzero exit when anything is found, so this can gate CI directly.
    sys.exit(1 if total_irr else 0)

if __name__ == '__main__':
    main()
