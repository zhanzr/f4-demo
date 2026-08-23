# fire-f429 / app — SDRAM-remapped projects

This folder holds projects that use the onboard SDRAM as normal runtime memory.
`blink_hello` reuses the bare blink + ADC sources, while its linker script puts
`.data`, `.bss`, and the heap at `0xD0000000`. `SystemInit()` initializes FMC
SDRAM through the HAL before the startup code copies `.data` and clears `.bss`.
The HAL tick and SDRAM setup state remain in internal RAM until that
initialization is complete.

The benchmark apps use the same source files and flags as their bare SRAM
counterparts. On hardware, SDRAM placement reduced Dhrystone from 391,198 to
175,700.609 Dhrystones/s and CoreMark from 470.234 to 194.246 iterations/s.
Both validations remained correct; the difference is performance, not data
integrity.

The hardware-verified startup output includes:

```text
pointers: data=0xd0000000 bss=0xd0000888 _sdata=0xd0000000 _edata=0xd0000888 _sbss=0xd0000888 _ebss=0xd0000a9c
```

Bare-metal projects that use only built-in flash + SRAM live in the sibling
`bare/` folder instead.