/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca memory map (Wild: the RAM regions in a new order, see below).
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * RAM  : .data/.bss
 * RAMTEXT holds .ram_text: the code that must run while the flash is busy (the flash driver), then
 *        .ram_dsp (NFM_HOT): the audio's hot loops and the tables they read, run from RAM rather
 *        than through the flash's cache. A call reaches +-4 MiB: RAMTEXT sits at the top, close
 *        enough to the XIP code for the DSP to call it and be called (the pool went to the bottom,
 *        RAM-run debug's 8 KiB went to RAMTEXT). The PC guard's second window spans it all.
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vectors. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    POOL   (rw)  : ORIGIN = 0x01C00000, LENGTH = 0x54000
    RAM    (rwx) : ORIGIN = 0x01C54000, LENGTH = 88K
    RAMTEXT (rwx): ORIGIN = 0x01C6A000, LENGTH = 40K
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_dsp = .; *(.ram_dsp*) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .data : { . = ALIGN(4); _data_start = .; *(.ram_dsp*) . = ALIGN(4); *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
