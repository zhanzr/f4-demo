# LTO and Dhrystone: why the "2×" score is an artifact

**Short version:** Dhrystone is trivially vulnerable to whole-program
optimization. With GCC `-flto` the compiler sees the entire benchmark at once,
hoists the (loop-invariant) work out of the timed loop, and the score jumps
~2.2× — 770,713 vs 351,370 Dhrystones/s on this board. The result still *looks*
correct (the hoisted code still runs once, so the final values match), which is
exactly why this number must never be quoted. This is a **known, documented
weakness of Dhrystone itself**, not a bug in our toolchain or setup.

## What Dhrystone actually measures

Dhrystone 2.1 is a single `for` loop (here 2,000,000 runs) calling
`Proc1`/`Proc2`/`Proc3`/`Func1`…`Func3` on a small set of globals. Two
structural facts make it optimizable:

- The loop body has a lot of **loop-invariant work** — string copies of
  constant-length, aligned strings, fixed arithmetic, and assignments that
  depend only on values that never change across iterations.
- The final value of every global is the **same after every iteration**
  (they are reset or re-derived each pass), so a compiler can compute the
  result of one iteration and conclude the rest are redundant.

The benchmark was published in 1984/1988, before compilers could see across
translation units, so its "verification" (compare final globals against
expected constants) only catches blatant dead-code elimination — not
loop-invariant hoisting, which is legal and preserves the final values.

## What LTO does here

Per-object `-Ofast` compiles each `.c` file alone: `dhry_1.c` cannot see the
repeat loop in `main.c`, so it must keep every statement. GCC `-flto` exports
GIMPLE instead of machine code, and the link-time plugin re-runs optimization
on the **whole program**. It then:

1. inlines `Proc*`/`Func*` into the timed loop,
2. proves much of the body is loop-invariant across the fixed 2,000,000
   iterations, and
3. **hoists it out of the loop** (loop-invariant code motion) and/or deletes
   redundant recomputation.

The timed region shrinks to a skeleton that re-checks a couple of values per
iteration. The remaining (hoisted) code still executes — once — so the printed
`Int_Glob`, `Arr_2_Glob`, etc. are still correct and the built-in check passes.

## Measured evidence (STM32F407VET6 @ 168 MHz, `-Ofast`)

| Build                | Flags                                     | µs/run | Dhrystones/s | DMIPS/MHz |
| -------------------- | ----------------------------------------- | ------ | ------------ | --------- |
| GCC 15.3.1           | `-Ofast -ffp-contract=fast -funroll-loops` | 2.85  | 351,370     | 1.190     |
| GCC 15.3.1 + LTO     | above `+ -flto`                            | 1.30  | 770,713     | 2.611 ⚠   |
| armclang 6.24 (AC6)  | `-Ofast -ffp-contract=fast -funroll-loops` | 2.54  | 393,391     | 1.333     |

- Non-LTO GCC and armclang agree with each other within ~12 % — consistent,
  meaningful numbers.
- The LTO build runs **2.2× faster per iteration** with identical final
  values. Per-run time drops from 2.85 µs to 1.30 µs; the "extra" 1.55 µs of
  work was simply moved out of the timed region.
- This is the same mechanism reported elsewhere: a public aarch64 example
  shows Dhrystone inflating from 5.2 M to 19.5 M Dhrystones/s (~3.7×) with
  `-flto` and a 3.5× drop in executed instructions.

## It's a known issue

Not a local anomaly — the Dhrystone-vs-LTO problem is documented by the
benchmark's own ecosystem:

- **EEMBC** (CoreMark authors): *"major portions of Dhrystone actually expose
  the compiler's ability to optimize the workload rather than the capabilities
  of an MCU."* CoreMark was designed to fix exactly this: every operation
  derives values unavailable at compile time, and a per-run CRC forces the
  work to execute, so nothing can be hoisted or pre-computed.
- **Arm's compiler blog** footnotes: *"Dhrystone can be compromised by the
  compiler by pre-computing values at compile time or optimizing away timed
  portions of the code."*
- **Wikipedia (Dhrystone/DMIPS)**: modern compiler static-analysis
  techniques (dead-code elimination, etc.) "make the use and design of
  synthetic benchmarks more difficult"; Dhrystone 2.0's anti-compiler
  changes were "only partly successful".
- **EE Journal, "Dhrystone Is Dead; Long Live CoreMark!"**: there is a big
  incentive to tweak Dhrystone "sometimes to the point where it compiles into
  little more than a sequence of NOPs."

## Why CoreMark doesn't break

CoreMark's timed section is CRC-protected on every run. If the compiler
hoisted or skipped the list/matrix/state-machine work, the printed
`crcfinal` would change — the benchmark would *fail* validation, not silently
inflate. In this repo GCC+LTO CoreMark (447.57 vs 448.55 iterations/s, ~0.2 %)
confirms LTO adds nothing measurable to CoreMark, consistent with the
StackOverflow observation that CoreMark is insensitive to LTO.

## Rules for using this repo's numbers

1. **Never quote an LTO Dhrystone score.** It measures a compiler trick, not
   the CPU.
2. Use the non-LTO columns for all normal GCC-vs-armclang comparison
   (README tables).
3. If a production build genuinely uses `-flto`, keep the benchmark build
   LTO-free (or use CoreMark) so the numbers stay comparable to non-LTO
   builds of other projects.
4. The `-flto` build here is kept **only** as reproducible evidence of the
   artifact (`build-gcc-lto/`).

## How to reproduce

```bash
cd dhry_168m
cmake -G Ninja -B build-gcc       -DSTM32_TOOLCHAIN=gcc            ..
cmake -G Ninja -B build-gcc-lto   -DSTM32_TOOLCHAIN=gcc -DSTM32_LTO=ON ..
ninja -C build-gcc;    ninja -C build-gcc-lto     # then flash + capture console
```

(GCC-LTO linking required `board/syscalls.c` to be compiled with `-fno-lto`,
see `../../cmake/stm32f407_board.cmake`.) Capture the console per the
board-level `../../README.md` and compare `Microseconds for one run` /
`Dhrystones per Second`.
