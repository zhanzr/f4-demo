# fire-f429 / app — SDRAM-remapped projects

**Placeholder.** This folder holds projects that run from **remapped external
SDRAM** (the fire-f429 board carries SDRAM mapped into the CPU memory space,
so code/data can execute from it).

Nothing is here yet. When SDRAM-based projects are added they will live here,
using the shared `../board/` and `../cmake/` support (with a linker script /
memory setup that maps the SDRAM region).

Bare-metal projects that use only built-in flash + SRAM live in the sibling
`bare/` folder instead.