# CoreMark 1.0.1 from SRAM @ 100 MHz — nano-f411 (STM32F411CEU6)

Runs CoreMark 1.0.1 (**10,000 iterations**) with the timed benchmark kernel
(`core_*.c`) linked into **SRAM** (0x20000000, the single 128 KB SRAM block of
the F411) and copy-in'd from flash at startup; the harness/HAL/printf stay in
flash. Goal: isolate pure CPU+SRAM throughput from flash wait-state / ART
influence. Timed with the HAL SysTick 1 kHz tick (`HAL_GetTick()`, see
`src/core_portme.c`).

## Why SRAM, and a note on flash vs SRAM

The F411 has one contiguous 128 KB SRAM region. The benchmark core is placed
at the SRAM **base** — the largest spot, and one where neither the copier
(`startup_stm32f411xx.s`) nor the linker places conflicting data.

Executing *memory* changes CoreMark throughput on this core (byte-identical
kernel code, SysTick timing on the same board; default gcc flags
`-Ofast -ffp-contract=fast -funroll-all-loops`):

| Execution memory (nano-f411 @ 100 MHz) | GCC it/s | Time (s) |
| -------------------------------------- | -------- | -------- |
| FLASH (ART I-cache, `coremark_100m`) | 282.35 | 35.42 |
| SRAM @ 0x20000000 (this project) | 207.91 | 48.10 |

**FLASH-with-ART is ~1.36× faster than SRAM:** the F4 flash is fetched over the
dedicated ICode bus through the ART accelerator (128-bit prefetch + instruction
cache), so tight loops run at near 1 instr/cycle with no bus arbitration. SRAM
has no such accelerator and is fetched over the shared system bus, so it is
slower than flash — the opposite of the usual "RAM is faster" intuition. The
disassembly of `core_bench_list` is byte-for-byte identical between the flash
and SRAM builds, confirming the difference is purely the fetch bus / ART, not
compiler output or a timing artifact.

## Results

Measured on hardware (100 MHz, hard-float). Same run as `coremark_100m` but
with the kernel in SRAM; that project's flash run is the "FLASH (ART I-cache)"
reference line above.

| Toolchain | Flags | Iterations/Sec (SRAM) | Time (s) |
| --------- | ----- | --------------------- | -------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-all-loops` (default) | **207.91** | 48.10 |
| ARMCLANG (Keil AC6) | `-Omax -fno-lto` | **244.15** | 40.96 |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 185.82 | 53.82 |

All runs print `Correct operation validated` with `crcfinal = 0x988c`.

Per toolchain, only the highest measured configuration is shown. Keil AC6's
`-Omax` (with `-fno-lto`) applies in SRAM too (244.15 it/s), but note the
flash version of the same build reaches **339.997** it/s, so even at `-Omax`
FLASH+ART stays ~1.39× faster than SRAM. ST's published "341" for the
SRAM-based run is not reproduced here; the flash 339 is (see
`coremark_100m/README.md`).

### Timing method: SysTick, not DWT/CYCCNT

Earlier versions of these builds timed the kernel with the DWT cycle counter
(CYCCNT). The GCC and ARMCLANG builds timed correctly with it, but the **ST
Arm clang build miscompiled the DWT-based timing**: `-Ofast` (and even `-O2`)
produced a self-flagged invalid result ("Must execute for at least 10 secs")
with a physically implausible rate, while the CPU and CYCCNT were both running
at the true clock. This is a clang optimizer interaction with the DWT-timing
control flow in `core_main.c`, not a board/clock/HAL fault. Switching
`core_portme.c` to the HAL SysTick 1 kHz tick (a call into the HAL) yields
consistent, self-validating runs on all three toolchains, which is why this
project times with SysTick.

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs via probe-rs / ST-Link (SWD)
# armclang / starm-clang via -DSTM32_TOOLCHAIN=... as in coremark_100m
# armclang -Omax (244.15 it/s here): -DBENCH_OPT= -DBENCH_OPT_C="-Omax -fno-lto"
```

Console is the board's USART1 / ST-Link VCP (115200 8-N-1) — see
`../../README.md`.