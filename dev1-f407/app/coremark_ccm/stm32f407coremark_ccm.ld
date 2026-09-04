/* GNU ARM GCC linker script for the dev1-f407 CoreMark-in-CCM project.
 *
 * Memory split (STM32F407VET6):
 *   FLASH : 512 KB @ 0x08000000   - vector table, newlib/HAL/harness code
 *   SRAM  : 128 KB @ 0x20000000   - .data/.bss, heap, stack (as the normal board)
 *   CCM   : 64 KB  @ 0x10000000   - .ram_code: the timed CoreMark core runs here
 *
 * CCM is the Cortex-M4 core-only SRAM: single-cycle, reached on the I-bus/D-bus
 * and therefore able to both fetch instructions and hold data. It is NOT
 * reachable by DMA, but the benchmark uses only core memory access. Running the
 * timed loops from CCM removes both flash wait-states/ART and the shared SRAM
 * arbitrations of the data path, isolating the core's raw execution speed.
 * The harness (main, core_portme, HAL, printf) stays in FLASH.
 *
 * Symbols consumed by startup_stm32f407xx.s: _estack, _sidata, _sdata, _edata,
 *   _sbss, _ebss (existing) plus _sram_code/_eram_code/_sram_code_idata for the
 *   .ram_code copy-in. syscalls.c sbrk: end / _end.
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (xrw)     : ORIGIN = 0x20000000, LENGTH = 128K
  CCM   (xrw)     : ORIGIN = 0x10000000, LENGTH = 64K
}

_estack       = ORIGIN(RAM) + LENGTH(RAM);  /* top of main SRAM, stack grows down */
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
    *(.text.Reset_Handler)
    EXCLUDE_FILE(*core_list_join.c.obj *core_matrix.c.obj *core_state.c.obj *core_util.c.obj *core_main.c.obj)
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
    EXCLUDE_FILE(*core_list_join.c.obj *core_matrix.c.obj *core_state.c.obj *core_util.c.obj *core_main.c.obj)
      *(.rodata)
    EXCLUDE_FILE(*core_list_join.c.obj *core_matrix.c.obj *core_state.c.obj *core_util.c.obj *core_main.c.obj)
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
  } >RAM AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /* DMA buffers kept for source compatibility with the normal board. */
  .sram_dma (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sram_dma)
    . = ALIGN(32);
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* The timed CoreMark kernel: code + const data relocated to CCM and copy-in'd
   * from flash at startup (symbols drive the startup .ram_code loop). */
  .ram_code :
  {
    _sram_code = .;
    *core_list_join.c.obj(.text* .rodata*)
    *core_matrix.c.obj(.text* .rodata*)
    *core_state.c.obj(.text* .rodata*)
    *core_util.c.obj(.text* .rodata*)
    *core_main.c.obj(.text* .rodata*)
    . = ALIGN(4);
    _eram_code = .;
  } >CCM AT> FLASH
  _sram_code_idata = LOADADDR(.ram_code);

  /* Stabs debugging sections. */
  .stab          0 : { *(.stab) }
  .stabstr       0 : { *(.stabstr) }
  .stab.excl     0 : { *(.stab.excl) }
  .stab.exclstr  0 : { *(.stab.exclstr) }
  .stab.index    0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment       0 : { *(.comment) }
  /* DWARF debug sections. */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  .debug_info     0 : { *(.debug_info) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
  .debug_pubtypes 0 : { *(.debug_pubtypes) }
  .debug_ranges   0 : { *(.debug_ranges) }
  .debug_macro    0 : { *(.debug_macro) }
  .ARM.attributes 0 : { KEEP (*(.ARM.attributes)) }
  .note.gnu.arm.ident 0 : { KEEP (*(.note.gnu.arm.ident)) }
}
