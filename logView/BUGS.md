# BUGS.md

1) timekeeping in text display is wrong. As soon as the first timestamped event is decoded, the time value becomes a huge positive number:

0x0000008b: 1E 79 0B    [    73  @     0.0000s]: HOFLO       : 2937
0x0000008e: 67 EF       [    74  @ 562949.9534s]: PORTG_DB    : 0b11101111

