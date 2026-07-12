#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Henrique Bucher
# Build the loop-hunter out-of-tree LLVM pass plugin.
# Needs LLVM 20 dev (llvm-config-20). No CMake required.
set -e
cd "$(dirname "$0")"

LLVM_CONFIG="${LLVM_CONFIG:-llvm-config-20}"
CLANG="${CLANG:-clang++-20}"

# llvm-config's --cxxflags carries its own -std; strip it so ours wins.
CXXFLAGS="$("$LLVM_CONFIG" --cxxflags | sed 's/-std=[^ ]*//')"

echo "building libLoopHunter.so with $($LLVM_CONFIG --version)"
$CLANG -shared -fPIC -fno-rtti -std=c++17 $CXXFLAGS \
    LoopHunter.cpp -o libLoopHunter.so
echo "ok: $(ls -la libLoopHunter.so | awk '{print $5}') bytes"
