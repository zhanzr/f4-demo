# CoreMark 1.0.1 from CCM @ 168 MHz — dev1-f407 (STM32F407VET6)

Runs CoreMark 1.0.1 (**10,000 iterations**) with the timed benchmark kernel
(`core_*.c`) linked into **CCM** (0x10000000, 64 KB) and copy-in'd from flash
at startup. See `stm32f407coremark_ccm.ld`.

## Test status (measured on hardware, dev1-f407)

> **CCM code does not work on this board.**

Attempting to execute the kernel from CCM (0x10000000) faults immediately:
the CPU takes a hard fault (CFSR = BFSR.IBUSERR — bus error on instruction
fetch) the instant it fetches the first instruction at 0x10000000. The fault
is reproducible with a trivial 2-instruction function too (`ram_test`), and is
independent of the program running.

Notably, on this board:
- CCM **data** access works (a word written to 0x10001000 reads back correctly).
- SRAM2 **code** execution works fully (`coremark_sram`, 401.75 iters/s).
- The MPU is **disabled**, so this is not an MPU configuration fault.
- The CCM copy-in is byte-identical to the flash source (verified).

This is therefore recorded as a board/part behavior: **CCM code execution does
not work on this dev1-f407**. No root cause has been proven; treat it as an
observed test result on this specific board, not a general STM32F407 claim.

## Build

Same workflow as `coremark_sram` (`build.sh` / `ninja flash`). The build
produces a valid image but its execution faults on entry to CCM on this board.
