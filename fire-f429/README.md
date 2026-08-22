# fire-f429 — STM32F429xI development projects (placeholder)

**Placeholder board folder.** The `fire-f429` board will target an
STM32F429 (Cortex-M4F @ up to 180 MHz, 2 MB flash / 256 KB RAM, F42x/F43x
family).

Nothing is here yet. When the board is added, this folder will follow the same
layout as the other boards:

```
fire-f429/
├── README.md         board overview (hardware, clock tree, console)
├── app/              applications
├── board/            shared board layer (board.c/h, uart_printf, startup, linker script)
├── cmake/            toolchain + board helpers
└── ...               board photos / schematic / .ioc
```

The vendored `../drivers/` (F4 HAL + CMSIS) already covers the STM32F429
device headers (in `drivers/CMSIS/Device/ST/STM32F4xx/Include/`), so no HAL
vendoring is needed for this board.