/* A simple linker file for an Aprilia ECU EPROM.
 * It makes sure that the code is relocated to 0x8000..0xFFFF
 * where the ECU's HC11 is expecting it. Everything else in the
 * build gets discarded (like the .bss or .data sections).
 */
MEMORY {
  eprom (rx)  : ORIGIN = 0x8000, LENGTH = 32768
  ignored (r) : ORIGIN = 0x0000, LENGTH = 0
}

SECTIONS {
  .text :
    {
      *(.text)
      _etext = . ;
    } > eprom

  /DISCARD/ :
    {
      *( * )
    }

}
