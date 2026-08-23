/* GNU ARM GCC linker script for STM32F429IGT6 (fire-f429 board).
 *
 * Memory layout:
 *   FLASH : 1 MB   @ 0x08000000
 *   RAM   : 192 KB @ 0x20000000   SRAM1 112K + SRAM2 16K + SRAM3 64K
 *                                  (contiguous, accessible by all AHB masters)
 *   CCM   : 64 KB  @ 0x10000000   Core Coupled Memory - CPU-only (D-bus),
 *                                  0-wait, NOT accessible by DMA/AHB masters.
 *
 * CCM usage:
 *   - .ccmram section holds variables explicitly placed there via
 *     __attribute__((section(".ccmram")))  (CPU-only, but optimum for
 *     latency-critical data; see "Symbols" below).
 *   - .stack is placed in CCM (CPU-only access, never DMA'd) so SRAM1/2/3
 *     stay fully available to the application + AHB masters.
 *   - _sbrk() heap is limited to the top of RAM (see syscalls.c HEAP_LIMIT).
 *
 * Symbols consumed by startup_stm32f429xx.s:
 *   _estack, _sidata, _sdata, _edata, _sbss, _ebss
 * Symbols consumed by syscalls.c (_sbrk): end / _end
 * Symbols consumed by board/syscalls.c (_sbrk heap limit): _eram
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM (xrw)       : ORIGIN = 0x20000000, LENGTH = 192K
  CCM (xrw)       : ORIGIN = 0x10000000, LENGTH = 64K
  SDRAM (xrw)     : ORIGIN = 0xD0000000, LENGTH = 8M
}

/* Stack top in CCM (CPU-only 0-wait RAM). Stack grows down from _estack. */
_estack       = ORIGIN(CCM) + LENGTH(CCM);
/* Heap limit: top of normal SRAM (the heap itself stays in RAM). */
_eram         = ORIGIN(RAM)  + LENGTH(RAM);
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

  .text :
  {
    . = ALIGN(4);
    *(.text)
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
    *(.rodata)
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
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

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
  } >RAM

  . = ALIGN(8);
  _end = .; __end__ = .; end = .;
  PROVIDE ( end = . );

  /* Heap in normal SRAM (accessible by DMA if needed). */
  .heap :
  {
    . = ALIGN(8);
    __heap_start__ = .;
    . = . + _Min_Heap_Size;
    __heap_end__ = .;
  } >RAM

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

  /* LTDC framebuffer (800x480 RGB888, 1.125 MiB) in SDRAM. The bare build
   * initializes SDRAM at runtime before first touching the buffer. */
  .sdram_fb (NOLOAD) :
  {
    . = ALIGN(8);
    __sdram_fb_start__ = .;
    *(.sdram_fb)
    *(.sdram_fb*)
    . = ALIGN(8);
    __sdram_fb_end__ = .;
  } >SDRAM

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