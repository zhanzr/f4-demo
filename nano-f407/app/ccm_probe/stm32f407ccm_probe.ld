/* CCM-execution proof linker script for the dev1-f407 board.
 * Places functions in section(".ram_code") into CCM (0x10000000, 64 KB) and
 * copy-in's them from flash at startup. Harness/HAL/reset stay in flash.
 * Used to isolate whether executing code from CCM works at all on this board
 * independently of CoreMark.
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 512K
  SRAM1 (xrw)     : ORIGIN = 0x20000000, LENGTH = 112K
  CCM   (xrw)     : ORIGIN = 0x10000000, LENGTH = 64K
}

_estack       = ORIGIN(SRAM1) + LENGTH(SRAM1);
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
    EXCLUDE_FILE(*ccm_func.c.obj) *(.text*)
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
    EXCLUDE_FILE(*ccm_func.c.obj) *(.rodata)
    EXCLUDE_FILE(*ccm_func.c.obj) *(.rodata*)
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
  } >SRAM1 AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >SRAM1

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >SRAM1

  .ram_code :
  {
    _sram_code = .;
    *(.ram_code)
    *(.ram_code*)
    . = ALIGN(4);
    _eram_code = .;
  } >CCM AT> FLASH
  _sram_code_idata = LOADADDR(.ram_code);

  .ARM.attributes 0 : { KEEP (*(.ARM.attributes)) }
  .note.gnu.arm.ident 0 : { KEEP (*(.note.gnu.arm.ident)) }
}
