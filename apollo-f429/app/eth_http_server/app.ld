/* apollo-f429 app stage-2 linker script - runs entirely from SDRAM.
 *
 * Memory map:
 *   SDRAM (rwx): 32 MiB @ 0xC0000000  (FMC bank 1, W9825G6KH)
 *
 * The bootloader (app/boot) copies the whole image (vector table, code,
 * rodata and the initialized .data) from NAND offset 0 into SDRAM at
 * 0xC0000000 and jumps to the reset vector there. LMA == VMA for every
 * section, so the startup's .data copy loop is a no-op; only .bss is zeroed
 * by the startup, again in SDRAM (initialized by the bootloader first). The
 * app does NOT own the clock tree - see system_app.c.
 */

ENTRY(Reset_Handler)

MEMORY
{
  SDRAM (rwx)  : ORIGIN = 0xC0000000, LENGTH = 32M
  /* Stack only: kept in the built-in SRAM (0-wait internal), not shared with
   * the SDRAM-resident app image, so nested calls/ISRs never thrash the
   * external bus. Everything else runs from SDRAM. */
  SRAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 192K
}

/* Stack top in built-in SRAM (grows down). */
_estack        = ORIGIN(SRAM) + LENGTH(SRAM);
/* Heap limit: top of SDRAM (the heap stays in SDRAM, next to .bss). */
_eram          = ORIGIN(SDRAM) + LENGTH(SDRAM);
_Min_Heap_Size = 0x400;
_Min_Stack_Size = 0x2000;

/* RAM-execution code region symbols: the startup copies .ram_code like .data.
 * This app runs entirely from SDRAM (LMA == VMA), so there is no separate
 * .ram_code; keep the section empty (start == end -> no-op copy). */
PROVIDE(_sram_code = 0);
PROVIDE(_eram_code = 0);
PROVIDE(_sram_code_idata = 0);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >SDRAM

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
  } >SDRAM

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >SDRAM

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >SDRAM

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >SDRAM

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >SDRAM

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >SDRAM

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >SDRAM

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
  } >SDRAM

  .early_bss (NOLOAD) :
  {
    . = ALIGN(4);
    _searly_bss = .;
    *(.bss.uwTick)
    *(.early_bss)
    *(.early_bss*)
    . = ALIGN(4);
    _eearly_bss = .;
  } >SDRAM

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
  } >SDRAM

  . = ALIGN(8);
  _end = .; __end__ = .; end = .;
  PROVIDE ( end = . );

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = ALIGN(8);
  } >SDRAM

  /* Stack: kept in the built-in 0-wait SRAM (top, grows down) so the app's
   * call/ISR nesting never foots onto the SDRAM image it runs from. */
  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    __stack_start__ = .;
    . = . + _Min_Stack_Size;
    __stack_end__ = .;
  } >SRAM

  /* Ethernet DMA buffers (descriptors + RX pool + TX bounce) MUST live in
   * internal SRAM: on the F4 the ETH DMA cannot reach the external SDRAM.
   * NOLOAD - the driver initializes the descriptors before HAL_ETH_Start. */
  .sram_dma (NOLOAD) :
  {
    . = ALIGN(8);
    __sram_dma_start__ = .;
    *(.sram_dma)
    *(.sram_dma*)
    . = ALIGN(8);
    __sram_dma_end__ = .;
  } >SRAM

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