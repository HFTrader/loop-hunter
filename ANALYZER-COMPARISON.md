# Do existing analyzers already catch irreducible loops?

Short answer: **no.** Irreducibility sits in a blind spot across the entire
mainstream C++ diagnostics/static-analysis stack. Run on the synthetic battery
(`tests/`), here is what each tool does.

## Per-function results (clang-20 / gcc-14)

Irreducible functions (loop-hunter must, and does, flag all 6):

| function | shape | clang-tidy `avoid-goto` | `-Wimplicit-fallthrough` | gcc `-fopt-info-vec-missed` | loop-hunter |
|---|---|---|---|---|---|
| irr_two_node_core | goto | flags (wrong reason) | - | silent | **flags** |
| irr_jump_into_for | goto | flags (wrong reason) | - | silent | **flags** |
| irr_three_node | goto | flags (wrong reason) | - | silent | **flags** |
| irr_nested_inner | goto | flags (wrong reason) | - | silent | **flags** |
| irr_resumable_switch | switch | **MISSES** | flags (wrong reason) | silent | **flags** |
| irr_duffs_device | switch | **MISSES** | flags (wrong reason) | silent | **flags** |

Reducible functions (must NOT be flagged — false-positive check):

| function | shape | clang-tidy `avoid-goto` | loop-hunter |
|---|---|---|---|
| red_goto_again | goto | **FALSE ALARM** | correct (no flag) |
| red_two_latches | goto (2 latches) | **FALSE ALARM** | correct (no flag) |

## What fired, and why none of it is a substitute

- **clang-tidy `-checks='*'`**: dozens of warnings, **zero** mentioning
  irreducibility / vectorization / "won't be optimized". The only control-flow
  hit is `cppcoreguidelines-avoid-goto` / `hicpp-avoid-goto`, which is a blanket
  *style* rule on the `goto` keyword. As an irreducibility detector it is both
  **unsound** (fires on the two *reducible* goto loops = false alarms on safe
  code) and **incomplete** (the switch-based `goto`-free irreducible cases --
  the resumable parser and Duff's device, i.e. the hottest, most realistic
  ones -- produce no warning at all).
- **`-Wimplicit-fallthrough`** (clang & gcc): fires on the switch-based cases,
  but as a fallthrough-safety warning unrelated to irreducibility; silent on the
  goto-based irreducible loops; and noisy on countless legitimate switches.
- **clang `-Weverything`, gcc `-Wall -Wextra`, clang static analyzer,
  cppcheck `--enable=all`**: nothing about loops being irreducible/unoptimizable.
- **gcc `-fopt-info-vec-missed`** (the compiler's own "why didn't you
  vectorize" channel): **silent** on every irreducible function. The vectorizer
  never sees a loop there, so it cannot even report it as a missed loop.

## Takeaway

The one overlapping signal, `avoid-goto`, is a proxy for the wrong thing: it
punishes the safe `goto`-as-`while` and waves through the dangerous switch-based
resumable parser. Nothing in the standard toolchain models what "irreducible"
means, and the compiler's own missed-optimization reporter is blind to it by
construction. That is exactly the gap loop-hunter fills: sound (0 false alarms)
and complete (all 6 shapes, goto- and switch-based) because it asks the real
question -- "does this cycle have more than one entry?" -- not "is there a goto?"
