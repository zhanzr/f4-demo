/* GNU ARM GCC linker script for the fire-f429 CoreMark-in-SRAM1 project.
 *
 * Memory split (STM32F429IGT6):
 *   FLASH  : 1 MB  @ 0x08000000   - vector table, newlib/HAL/harness code
 *   SRAM1  : 112 KB @ 0x20000000  - .ram_code: the timed CoreMark core (base),
 *                                   then .data/.bss, heap, stack
 *   SRAM2  : 16 KB @ 0x2001C000  - unused jump hole (contiguous with SRAM1/3)
 *   SRAM3  : 64 KB @ 0x20020000  - unused (kept for reference / DMA scratch)
 *   CCM    : 64 KB @ 0x10000000  - CPU-only (D-bus), 0-wait
 *
 * The benchmark kernel (core_*.c) is placed in SRAM1 (via .ram_code at the SRAM1
 * base, copy-in'd from flash at startup by Reset_Handler) so the timed loops
 * execute from SRAM. The harness (main, core_portme, HAL, printf) stays in FLASH.
 *
 * Symbols consumed by startup_stm32f429xx.s: _estack, _sidata, _sdata, _edata,
 *   _sbss, _ebss (existing) plus _sram_code/_eram_code/_sram_code_idata for the
 *   .ram_code copy-in. syscalls.c sbrk: end / _end.
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 1024K
  SRAM1 (xrw)     : ORIGIN = 0x20000000, LENGTH = 112K
  SRAM2 (xrw)     : ORIGIN = 0x2001C000, LENGTH = 16K
  SRAM3 (xrw)     : ORIGIN = 0x20020000, LENGTH = 64K
  CCM (xrw)       : ORIGIN = 0x10000000, LENGTH = 64K
}

/* Stack top in CCM (CPU-only 0-wait RAM). Stack grows down from _estack. */
_estack       = ORIGIN(CCM) + LENGTH(CCM);
/* Heap limit: top of normal SRAM (the whole 192K: SRAM1+S2+S3). */
_eram         = ORIGIN(SRAM1) + LENGTH(SRAM1) + LENGTH(SRAM2) + LENGTH(SRAM3);
_Min_Heap_Size = 0x400;
_Min_Stack_Size = 0x1000;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  /* The timed CoreMark kernel: code + const data relocated to SRAM1 and
   * copy-in'd from flash at startup (symbols drive the startup .ram_code loop).
   * Placed at the SRAM1 base so the file-qualified globs win GNU ld's
   * first-match and claim the core kernel regardless of compiler (gcc,
   * armclang). Each core object may be named core_*.c.obj (gcc) or
   * core_*.o (armclang). */
  .ram_code :
  {
    _sram_code = .;
    *core_list_join.c.obj(.text* .rodata*) *core_list_join.o(.text* .rodata*)
    *core_matrix.c.obj(.text* .rodata*)   *core_matrix.o(.text* .rodata*)
    *core_state.c.obj(.text* .rodata*)    *core_state.o(.text* .rodata*)
    *core_util.c.obj(.text* .rodata*)     *core_util.o(.text* .rodata*)
    *core_main.c.obj(.text* .rodata*)     *core_main.o(.text* .rodata*)
    . = ALIGN(4);
    _eram_code = .;
  } >SRAM1 AT> FLASH
  _sram_code_idata = LOADADDR(.ram_code);

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    EXCLUDE_FILE(*core_list_join.c.obj *core_list_join.o *core_matrix.c.obj *core_matrix.o *core_state.c.obj *core_state.o *core_util.c.obj *core_util.o *core_main.c.obj *core_main.o)
      *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    EXCLUDE_FILE(*core_list_join.c.obj *core_list_join.o *core_matrix.c.obj *core_matrix.o *core_state.c.obj *core_state.o *core_util.c.obj *core_util.o *core_main.c.obj *core_main.o)
      *(.rodata)
    EXCLUDE_FILE(*core_list_join.c.obj *core_list_join.o *core_matrix.c.obj *core_matrix.o *core_state.c.obj *core_state.o *core_util.c.obj *core_util.o *core_main.c.obj *core_main.o)
      *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    __data_start__ = _sdata;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
    __data_end__ = _edata;
  } >SRAM1 AT> FLASH

  .early_bss (NOLOAD) :
  {
    . = ALIGN(4);
    _searly_bss = .;
    *(.bss.uwTick)
    *(.early_bss)
    *(.early_bss*)
    . = ALIGN(4);
    _eearly_bss = .;
  } >SRAM1

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >SRAM1

  . = ALIGN(8);
  _end = .; __end__ = .; end = .;
  PROVIDE ( end = . );

  /* Heap in normal SRAM. */
  .heap :
  {
    . = ALIGN(8);
    __heap_start__ = .;
    . = . + _Min_Heap_Size;
    __heap_end__ = .;
  } >SRAM1

  /* CCM: zero-init data placed here explicitly (NOLOAD - CPU-only RAM). */
  .ccmram (NOLOAD) :
  {
    . = ALIGN(8);
    __ccmram_start__ = .;
    *(.ccmram)
    *(.ccmram*)
    . = ALIGN(8);
    __ccmram_end__ = .;
  } >CCM

  /* Stack in CCM (CPU-only, 0-wait; not DMA-accessible by design). */
  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    __stack_start__ = .;
    . = . + _Min_Stack_Size;
    __stack_end__ = .;
  } >CCM

  /* Remove unused sections from the output. */
  /DISCARD/ :
  {
    libc.a(*)
    libm.a(*)
    libgcc.a(*)
  }
}