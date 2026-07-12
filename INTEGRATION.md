# Plugging loop-hunter into your build

Two ways to consume it:

- **Mode A (inline):** add one flag to a **clang** build; the hunter warns
  during every normal compile, in your build log / IDE. Best DX.
- **Mode B (scan):** run it in CI over `compile_commands.json`. Works even if
  you compile with gcc. Exits nonzero when anything is found, so it gates.

## The one rule you cannot skip

An LLVM pass plugin has **no cross-version ABI stability**. You must build
`libLoopHunter.so` against the **same LLVM major release** as the `clang`/`opt`
that will load it. Mismatch = it fails to load. Check with:

    clang++ --version          # e.g. clang 20.x
    llvm-config-20 --version   # must match the major version

Build against that LLVM:

    LLVM_CONFIG=llvm-config-20 ./build.sh
    # or
    cmake -DLLVM_DIR=$(llvm-config-20 --cmakedir) -B build -S . && cmake --build build

Ship `LoopHunter.cpp` + `CMakeLists.txt` (or `build.sh`) in your repo (or vendor
it as a submodule); each consumer builds the `.so` against their own LLVM. Do
not distribute a prebuilt `.so` unless you pin the LLVM version exactly.

## Mode A — inline during a clang build

The plugin auto-registers at the vectorizer-start extension point, so no
`-passes=` is needed. Just add the flag (absolute path) and build with `-g` for
source locations. It is read-only: **it never changes your codegen**, only
prints warnings.

### CMake

```cmake
# Only meaningful with clang; guard so gcc builds are unaffected.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-fpass-plugin=${CMAKE_SOURCE_DIR}/third_party/loop-hunter/libLoopHunter.so)
endif()
```

Scope it to hot targets instead of the whole tree with
`target_compile_options(myfeedhandler PRIVATE -fpass-plugin=.../libLoopHunter.so)`.

### Make

```make
ifeq ($(findstring clang,$(CXX)),clang)
  CXXFLAGS += -fpass-plugin=$(abspath third_party/loop-hunter/libLoopHunter.so)
endif
```

### Bazel

```python
# one target
cc_library(
    name = "feed_handler",
    srcs = ["feed_handler.cc"],
    copts = ["-fpass-plugin=$(location //third_party/loop-hunter:libLoopHunter.so)"],
    data = ["//third_party/loop-hunter:libLoopHunter.so"],
)
# or globally:  bazel build --copt=-fpass-plugin=/abs/path/libLoopHunter.so //...
```

### Make it fail the build (not just warn)

`-fpass-plugin` warnings don't set a nonzero exit. To turn a finding into a hard
error, filter the compiler's stderr in CI, e.g. wrap the compiler:

```sh
# cc-wrapper.sh: fail if the hunter fired
clang++ "$@" 2> >(tee /tmp/cc.err >&2)
grep -q 'irreducible loop' /tmp/cc.err && { echo "loop-hunter: irreducible loop"; exit 1; } || true
```

For clean gating, prefer Mode B.

## Mode B — CI scan over compile_commands.json

Works regardless of your build compiler (it re-issues each TU as IR with clang).
It exits nonzero when it finds anything.

```yaml
# .github/workflows/loop-hunter.yml
- run: |
    sudo apt-get install -y clang-20 llvm-20-dev
    LLVM_CONFIG=llvm-config-20 ./third_party/loop-hunter/build.sh
    ./third_party/loop-hunter/scan-codebase.py build/compile_commands.json \
        --clang clang++-20 --opt opt-20 --opt-level -O2
```

`--limit N` scans a subset; `-j N` sets parallelism. Generate
`compile_commands.json` with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` or `bear`.

## Editor / CI annotations

Output is `path:line: warning: ... [loop-hunter]` plus `note:` lines -- the same
shape clang uses. VS Code's C/C++ problem matcher, `-fdiagnostics-format`, and
GitHub's built-in clang matcher surface it as inline annotations with no extra
config. A minimal GitHub problem matcher:

```json
{ "problemMatcher": [{ "owner": "loop-hunter",
  "pattern": [{ "regexp": "^(.+):(\\d+): (warning): (.+)$",
                "file": 1, "line": 2, "severity": 3, "message": 4 }] }] }
```

## Which opt level to scan

Scan at the `-O` level you ship. Higher opt levels can *create* irreducibility
(jump threading / tail duplication turning a reducible loop irreducible), so a
loop that is clean at `-O1` can be irreducible at `-O2`/`-O3`. Mode A uses
whatever level your build already passes; for Mode B set `--opt-level`.

## Gotchas

- **gcc can't load it.** It's an LLVM plugin. gcc shops use Mode B (which shells
  out to clang for IR) or a clang-based CI lane.
- **-O0 prints nothing in Mode A**: the vectorizer-start extension point isn't
  in the -O0 pipeline. Scan at -O1+.
- **Absolute paths** for `-fpass-plugin`; relative paths break under build
  sandboxes (Bazel) and out-of-tree builds.
