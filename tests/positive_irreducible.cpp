// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Henrique Bucher
// POSITIVE tests: every function here contains a genuinely IRREDUCIBLE loop
// (a cycle with more than one entry). The loop-hunter MUST flag each one.
// extern "C" so names are unmangled -> the test runner reads them directly.
//
// Naming convention: functions that must be flagged start with `irr_`.

#include <cstdint>

struct P { int state; int i; uint32_t c; };

extern "C" {

// 1) Classic 2-node irreducible core: A<->B, entered at A (fallthrough) or B (goto).
uint32_t irr_two_node_core(const uint32_t* a, int n, int enter_b) {
    uint32_t s = 0; int i = 0;
    if (enter_b) goto B;
A:  if (i >= n) return s;
    s += a[i]; ++i; goto B;
B:  if (i >= n) return s;
    s += a[i] * 2; ++i; goto A;
}

// 2) Jump into the middle of a for-loop (skip the first-iteration guard).
uint32_t irr_jump_into_for(const uint32_t* a, int n, int skip) {
    uint32_t s = 0; int i = 0;
    if (skip) goto body;
    for (; i < n; ++i) { body: s += a[i]; }
    return s;
}

// 3) Resumable / Tatham-coroutine parser: case label inside the loop body.
uint32_t irr_resumable_switch(P* st, const uint32_t* p, int n) {
    switch (st->state) {
    case 0:
        st->c = 0;
        for (st->i = 0; st->i < n; ++st->i) {
            st->state = 1;
    case 1:
            st->c += p[st->i];
        }
    }
    return st->c;
}

// 4) Duff's device: switch dispatches into the middle of a do/while.
void irr_duffs_device(char* to, const char* from, int count) {
    int n = (count + 7) / 8;
    switch (count % 8) {
    case 0: do { *to++ = *from++;
    case 7:      *to++ = *from++;
    case 6:      *to++ = *from++;
    case 5:      *to++ = *from++;
    case 4:      *to++ = *from++;
    case 3:      *to++ = *from++;
    case 2:      *to++ = *from++;
    case 1:      *to++ = *from++;
            } while (--n > 0);
    }
}

// 5) Three-node cycle A->B->C->A with two entries (A fallthrough, B goto).
uint32_t irr_three_node(const uint32_t* a, int n, int enter_b) {
    uint32_t s = 0; int i = 0;
    if (enter_b) goto B;
A:  if (i >= n) return s; s += a[i]; ++i; goto B;
B:  if (i >= n) return s; s += a[i]; ++i; goto C;
C:  if (i >= n) return s; s += a[i]; ++i; goto A;
}

// 6) Reducible OUTER for-loop containing an irreducible inner region.
//    Tests that the hunter recurses and flags the nested irreducible cycle.
uint32_t irr_nested_inner(const uint32_t* a, int n, int m, int enter_b) {
    uint32_t s = 0;
    for (int o = 0; o < m; ++o) {
        int i = 0;
        if (enter_b) goto B;
A:      if (i >= n) goto next; s += a[i]; ++i; goto B;
B:      if (i >= n) goto next; s += a[i] * 2; ++i; goto A;
next:;
    }
    return s;
}

} // extern "C"
