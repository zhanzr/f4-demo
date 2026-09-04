/* GNU ARM linker script for the fire-f429 coremark_sram app: kernel in SRAM1,
 * runtime data in onboard SDRAM (app convention, as stm32f429_sdram.ld).
 *
 * Memory split (STM32F429IGT6):
 *   FLASH  : 1 MB  @ 0x08000000   - vector table, newlib/HAL/harness code
 *   SRAM1  : 112 KB @ 0x20000000  - .ram_code: the timed CoreMark core (base)
 *   SRAM2  : 16 KB @ 0x2001C000  - unused jump hole
 *   SRAM3  : 64 KB @ 0x20020000  - unused
 *   CCM    : 64 KB @ 0x10000000  - CPU-only (D-bus), 0-wait, stack
 *   SDRAM  : 8 MB  @ 0xD0000000  - .data/.bss/.heap (as the SDRAM apps)
 *
 * The benchmark kernel (core_*.c) is placed in SRAM1 (via .ram_code, copy-in'd
 * from flash at startup) so the timed loops execute from SRAM while the
 * harness's runtime data (and CoreMark's working data) lives in SDRAM.
 *
 * Symbols consumed by startup_stm32f429xx.s: _estack, _sidata, _sdata, _edata,
 *   _sbss, _ebss (existing) plus _sram_code/_eram_code/_sram_code_idata for the
 *   .ram_code copy-in. syscalls.c sbrk: end / _end.
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 192K
  CCM    (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
  SDRAM  (xrw) : ORIGIN = 0xD0000000, LENGTH = 8M
}

_estack = ORIGIN(CCM) + LENGTH(CCM);
_eram = ORIGIN(SDRAM) + LENGTH(SDRAM);
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

  /* The timed CoreMark kernel: code + const data relocated to SRAM1 (RAM base)
   * and copy-in'd from flash at startup. Each core object may be named
   * core_*.c.obj (gcc) or core_*.o (armclang). */
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
  } >RAM AT> FLASH
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
    KEEP(*(.init))
    KEEP(*(.fini))
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

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >SDRAM AT>FLASH

  .early_bss (NOLOAD) :
  {
    . = ALIGN(4);
    _searly_bss = .;
    *(.bss.uwTick)
    *(.early_bss)
    *(.early_bss*)
    . = ALIGN(4);
    _eearly_bss = .;
  } >RAM

  /* DMA-accessible buffers must be in internal SRAM (F4 DMA cannot reach SDRAM). */
  .sram_dma (NOLOAD) :
  {
    . = ALIGN(8);
    __sram_dma_start__ = .;
    *(.sram_dma)
    *(.sram_dma*)
    . = ALIGN(8);
    __sram_dma_end__ = .;
  } >RAM

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(.sdram_test)
    *(.sdram_test*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >SDRAM

  . = ALIGN(8);
  _end = .;
  __end__ = .;
  end = .;
  PROVIDE(end = .);

  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    __heap_start__ = .;
    . = . + _Min_Heap_Size;
    __heap_end__ = .;
  } >SDRAM

  /* LTDC framebuffer: 800x480 RGB888 (1.125 MiB), in SDRAM. */
  .sdram_fb (NOLOAD) :
  {
    . = ALIGN(8);
    __sdram_fb_start__ = .;
    *(.sdram_fb)
    *(.sdram_fb*)
    . = ALIGN(8);
    __sdram_fb_end__ = .;
  } >SDRAM

  .ccmram (NOLOAD) :
  {
    . = ALIGN(8);
    __ccmram_start__ = .;
    *(.ccmram)
    *(.ccmram*)
    . = ALIGN(8);
    __ccmram_end__ = .;
  } >CCM

  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    __stack_start__ = .;
    . = . + _Min_Stack_Size;
    __stack_end__ = .;
  } >CCM

  /DISCARD/ :
  {
    libc.a(*)
    libm.a(*)
    libgcc.a(*)
  }
}