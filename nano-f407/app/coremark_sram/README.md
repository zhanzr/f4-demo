# CoreMark 1.0.1 from SRAM2 @ 168 MHz — nano-f407 (STM32F407VET6)

Runs CoreMark 1.0.1 (**10,000 iterations**) with the timed benchmark kernel
(`core_*.c`) linked into **SRAM2** (0x2001C000, 16 KB) and copy-in'd from
flash at startup; the harness/HAL/printf stay in flash. Goal: isolate pure
CPU+SRAM throughput from flash wait-state / ART influence. Timed with the HAL
SysTick 1 kHz tick (`HAL_GetTick()`, see `src/core_portme.c`).

## Three-toolchain SRAM2 test status (measured on hardware, nano-f407 — healthy chip)

SRAM2 execution works on all three toolchains. The kernel is reliably placed
in SRAM2 via `stm32f407coremark_sram.ld`, which places `.ram_code` **before**
`.text` (so GNU ld's first-match hands the core kernel to SRAM2 for every
compiler) and matches each core object by both `.c.obj` (gcc / starm-clang)
and `.o` (armclang) names.

| Toolchain | Iterations/Sec | Time (s) | CRC (crcfinal) |
| --------- | -------------- | -------- | -------------- |
| GCC 15.3.1 (`-Ofast`) | **198.19** | 50.46 | 0x988c — validated |
| ARMCLANG (Keil AC6, clang 20) | **224.95** | 44.46 | 0x988c — validated |
| ST Arm clang (LLVM 21.1.1, `-Ofast`) | **188.75** | 52.98 | 0x988c — validated |

All three validate with `crcfinal = 0x988c` (`Correct operation validated`) and
their reported runtimes match measured wall-clock time.

### Timing method: SysTick, not DWT/CYCCNT

Earlier builds timed the kernel with the DWT cycle counter (CYCCNT). The GCC
and ARMCLANG builds timed correctly with it, but the **ST Arm clang build
miscompiled the DWT-based timing**: `-Ofast` (and even `-O2`) produced a
self-flagged invalid result ("Must execute for at least 10 secs") with a
physically implausible rate (thousands of iterations/s across a ~54 s wall
run). Diagnostics confirmed the CPU and CYCCNT both run at a true 168 MHz and
that the timing leaf functions are compiled correctly, so this is a clang
optimizer interaction with the DWT-timing control flow in `core_main.c`, not
a board/clock/HAL fault. Switching `core_portme.c` to the HAL SysTick
1 kHz tick (a call into the HAL) yields consistent, self-validating runs on
all three toolchains, which is why the project now times with SysTick.

> CCM (0x10000000) cannot run code on this part — see `ccm_probe` and the
> board README "RAM / CCM code-execution test status".

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs via probe-rs / ST-Link (SWD)
```

Console is the board's USART1 / ST-Link VCP (115200 8-N-1); on this setup
reachable via the CH340 USB-serial port (auto-detect, e.g. COM4) — see
`../../README.md`.
