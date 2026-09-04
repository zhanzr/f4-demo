# CoreMark 1.0.1 from SRAM2 @ 168 MHz — dev1-f407 (STM32F407VET6)

Runs CoreMark 1.0.1 (**10,000 iterations**) with the timed benchmark kernel
(`core_*.c`) linked into **SRAM2** (0x2001C000, 16 KB) and copy-in'd from
flash at startup; the harness/HAL/printf stay in flash. Goal: isolate pure
CPU+SRAM throughput from flash wait-state / ART influence. Timed with the DWT
cycle counter (see `src/core_portme.c`).

> ⚠️ **Timing caveat (applies to this board).** This port times with the DWT
> cycle counter. As documented for the nano-f407 SRAM port, DWT-based timing
> of SRAM-executed code can drift from wall-clock time (CYCCNT under-counts
> when the code fetches over the shared system bus), so the SRAM numbers on
> this page can look higher than the flash run of the same build. The flash
> port (`coremark_168m`) uses the same DWT source and agrees with the
> SysTick-based numbers on nano-f407 (449 ≈ 448.55), so flash is the reliable
> cross-board reference; treat the absolute SRAM rates here as directional.

## Test status (measured on hardware, dev1-f407)

| Toolchain | Flags | Iterations/Sec (SRAM2, DWT) | CRC (crcfinal) |
| --------- | ----- | -------------------------- | -------------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-all-loops` (default) | **460.13** | 0x988c |
| ARMCLANG (Keil AC6) | `-Omax -fno-lto` | **567.05** | 0x988c |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 424.68 | 0x988c |

Per toolchain, only the highest measured configuration is shown; ARMCLANG
`-Omax` (C-only, `BENCH_OPT_C="-Omax -fno-lto"`) is the fastest SRAM2 score,
the same recipe as `coremark_168m`. All runs print `Correct operation
validated` with `crcfinal 0x988c`.

Compare the same kernels executing from FLASH (ART I-cache, `coremark_168m`
on this board): GCC 449.00 / ARMCLANG 542.09 / ST 401.43 iterations/s.

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs via probe-rs / ULINK2 (SWD)
# armclang / starm-clang via -DSTM32_TOOLCHAIN=... as in coremark_168m
# armclang -Omax: -DBENCH_OPT= -DBENCH_OPT_C="-Omax -fno-lto"
```

Console is the board's USART3 / USB-serial (115200 8-N-1), same as
`blink_hello` — see `../../README.md`.
