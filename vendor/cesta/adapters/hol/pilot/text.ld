SECTIONS {
  . = 0x10000;
  .text : { *(.text) *(.text.*) }
  /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
