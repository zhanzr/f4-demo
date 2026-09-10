/* Linker script for the MT29F4G08 NAND probe-rs flash algorithm.
 *
 * Everything (code + literal pools) is placed in one loadable section at
 * 0x20000000 so the extracted binary blob is fully position-independent when
 * probe-rs loads it to its chosen RAM address.
 */

MEMORY
{
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x2000
}

SECTIONS
{
  .text :
  {
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
  } > RAM

  /DISCARD/ :
  {
    *(.ARM.exidx*)
    *(.ARM.attributes*)
    *(.comment*)
    *(.note*)
    *(.debug*)
  }
}