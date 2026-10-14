ENTRY(main)
SECTIONS {
    . = 0x1000;
    .text : ALIGN(4) { *(.literal .literal.*) *(.text .text.*) . = ALIGN(4); }
    .rodata : ALIGN(4) { *(.rodata .rodata.*) . = ALIGN(4); }
    .data : ALIGN(4) { *(.data .data.* .sdata .sdata.* .got .got.plt) . = ALIGN(4); }
    .bss : ALIGN(4) { *(.bss .bss.* .sbss .sbss.*) *(COMMON) . = ALIGN(4); }
    /DISCARD/ : { *(.comment .note.* .eh_frame) }
}
