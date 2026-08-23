# fire-f429 / app — SDRAM-remapped projects

This folder holds projects that use the onboard SDRAM as normal runtime memory.
`blink_hello` reuses the bare blink + ADC sources, while its linker script puts
`.data`, `.bss`, and the heap at `0xD0000000`. `SystemInit()` initializes FMC
SDRAM before the startup code copies `.data` and clears `.bss`.

Bare-metal projects that use only built-in flash + SRAM live in the sibling
`bare/` folder instead.