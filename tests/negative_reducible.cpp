// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Henrique Bucher
// NEGATIVE tests: every loop here is REDUCIBLE (single entry). The loop-hunter
// MUST NOT flag any of these -- they are exactly the shapes that look scary but
// are optimizer-safe. A false positive here is a bug.
//
// Naming convention: functions that must NOT be flagged start with `red_`.

#include <cstdint>

extern "C" {

// Plain counted loop.
uint32_t red_plain_for(const uint32_t* a, int n) {
    uint32_t s = 0;
    for (int i = 0; i < n; ++i) s += a[i];
    return s;
}

// do/while.
uint32_t red_do_while(const uint32_t* a, int n) {
    uint32_t s = 0; int i = 0;
    if (n <= 0) return 0;
    do { s += a[i]; ++i; } while (i < n);
    return s;
}

// goto-as-while: `again:` at top, `goto again` jumps UP. Single entry. Reducible.
uint32_t red_goto_again(const uint32_t* a, int n) {
    uint32_t s = 0; int i = 0;
again:
    if (i >= n) return s;
    s += a[i]; ++i; goto again;
}

// switch FULLY inside the loop body (every case breaks). Reducible.
uint32_t red_switch_in_loop(const uint32_t* a, int n, int mode) {
    uint32_t s = 0;
    for (int i = 0; i < n; ++i) {
        switch (mode) {
            case 0: s += a[i];      break;
            case 1: s += a[i] * 2;  break;
            default: s ^= a[i];     break;
        }
    }
    return s;
}

// Two latches, ONE header: two back-edges both target `head`. Single entry.
// Multiple back-edges != irreducible. Reducible.
uint32_t red_two_latches(const uint32_t* a, int n, int c) {
    uint32_t s = 0; int i = 0;
head:
    if (i >= n) return s;
    s += a[i]; ++i;
    if (c & i) goto head;   // latch 1
    s += 1;
    goto head;              // latch 2
}

// Data-dependent exit (non-countable) but single entry. Reducible.
// (Won't vectorize, but that's a DIFFERENT failure mode; not our target.)
uint32_t red_sentinel_break(const uint8_t* p, int n) {
    uint32_t s = 0;
    for (int i = 0; i < n; ++i) { if (p[i] == 0xFF) break; s += p[i]; }
    return s;
}

// Nested reducible loops.
uint32_t red_nested_loops(const uint32_t* a, int n, int m) {
    uint32_t s = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j)
            s += a[i] ^ a[j];
    return s;
}

// while with a pointer walk (strlen-shape). Single entry. Reducible.
uint32_t red_ptr_walk(const uint8_t* p) {
    uint32_t s = 0;
    while (*p) { s += *p; ++p; }
    return s;
}

} // extern "C"
