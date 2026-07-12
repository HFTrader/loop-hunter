# loop-hunter

An out-of-tree LLVM (new pass manager) plugin that finds **irreducible loops** —
the ones the optimizer silently refuses to vectorize, unroll, or hoist, and
about which it emits **no diagnostic**.

## Why

`LoopInfo` — the analysis that LICM, loop-unroll, loop-unswitch, and the
loop vectorizer all run on — only recognizes *natural* (reducible, single-entry)
loops. A loop whose cycle has more than one entry is **irreducible**: it gets
none of those passes, and neither clang nor gcc warns you. A resumable parser
(a `case` label inside a loop), a jump into the middle of a loop, or a
goto state machine can silently turn a hot loop 20-30x slower.

This pass uses `CycleInfo` (LLVM's `GenericCycleInfo`, the Wei/Tan/Chen
single-pass DFS), which captures *all* cycles including irreducible ones. A
cycle is irreducible iff it has more than one entry
(`Cycle::isReducible() == (Entries.size() == 1)`).

## Why a pass, not a clang-tidy check?

Because irreducibility is a property of the LLVM IR after inlining and CFG
cleanup, not of the source text. clang-tidy runs on the AST, one translation
unit, before any optimization, so it can only match syntax like "a `case` inside
a loop". But the same resumable-parser source is irreducible when the parser
state is runtime-unknown and perfectly reducible when the caller passes a
constant state the inliner can fold. A syntactic check false-positives on the
second case, and it misses irreducibility that jump threading manufactures at
`-O2`, where there is no source pattern to match at all. loop-hunter runs
`GenericCycleInfo` right before the vectorizer, at the `-O` level you actually
ship, so it reports the loops the optimizer really skips in the code it really
emits, not the ones that merely look risky.

## Build

    ./build.sh            # needs llvm-config-20 / clang++-20

Produces `libLoopHunter.so`.

## Use on one file

    clang++-20 -O2 -g -S -emit-llvm foo.cpp -o foo.ll
    opt-20 -load-pass-plugin=./libLoopHunter.so \
           -passes='function(loop-hunter),loop-hunter-summary' \
           -disable-output foo.ll

Example output (compiler-diagnostic style, so editors/CI parse it):

    parser.cpp:43: warning: irreducible loop in checksum_resumable(Parser*, unsigned char const*, int) (2 entries) -- vectorizer/LICM/unroll skip it silently [loop-hunter]
    parser.cpp:43: note: entry into the cycle via block %21
    parser.cpp:38: note: entry into the cycle via block %16
    note: fix: give the cycle a single entry (buffer-and-restart instead of resume-into-the-middle; don't jump or switch past the loop head)

Build with `-g` for source locations. Higher opt levels can *create*
irreducibility (jump threading / tail duplication), so scan at the `-O` level
you ship.

## Plug it into your build

See `INTEGRATION.md`. The short version: add one flag to a clang build and the
hunter warns inline, during every compile:

    CXXFLAGS += -fpass-plugin=/abs/path/to/libLoopHunter.so

or, for gcc shops / CI gating, run `scan-codebase.py` over your
`compile_commands.json` (it exits nonzero when anything is found).

## Use on a whole codebase

    ./scan-codebase.py /path/to/build/compile_commands.json

Re-issues every TU's compile as LLVM IR (via clang) and reports every
irreducible loop, or `CLEAN` if there are none.

## Tests

    ./build.sh && ./tests/run-tests.py

`tests/positive_irreducible.cpp` holds 6 known-irreducible shapes (2-node core,
jump-into-middle, resumable switch, Duff's device, 3-node cycle,
nested-irreducible-in-reducible); `tests/negative_reducible.cpp` holds 8
reducible shapes that must NOT be flagged (incl. multiple back-edges,
switch-in-loop, goto-again, non-countable exit). The runner asserts every
function is classified correctly across `-O0/-O1/-O2/-O3` (56/56).

## Files

- `LoopHunter.cpp` — the pass.
- `build.sh` — one-shot build (no CMake).
- `CMakeLists.txt` — CMake build alternative.
- `scan-codebase.py` — run across a compile_commands.json.
- `tests/` — synthetic positive/negative validation + runner.

## License

Apache-2.0 (see `LICENSE` and `NOTICE`). Copyright 2026 Henrique Bucher.
You may use, modify, and vendor this into your build; keep the copyright
and attribution notice.
