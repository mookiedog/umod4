# To-Do List

## New Features

### Reflash EP

A user would use a server command to push an EP.uf2 file onto the WP's filesystem
and instruct the WP to reflash the EP with that new uf2 file.

In high-level terms, it would work like this:

* WP would issue a reset to the EP which would have a side effect of holding the ECU in reset
* WP would call the reflash code
  * release EP resets, then put the EP into debug mode
  * WP reprograms WP-WP serial link as it needs
  * use an SWD loader program to install flasher code in the EP RAM, then execute it
  * flasher code configures the EP-WP serial lines appropriately for bidirectional communication
    * for simplicity, it might drive a 32-bit UART in both directions
    * the issue of who services the RS32 UART interrupt in the WP needs to be solved
  * WP would sync up with EP and tell it to get ready for a uf2 file
  * UF2 file would get sent over in sectors? or pages? and the EP coalesces into sectors?
  * EP would flash an entire sector, then verify its contents, then signal WP with pass/fail and wait for next sector
  * after WP sends last sector and it is accepted, it can simply reboot the EP via the 'RUN' line: EP needs no special code
* WP cleans up communication and ISR handing, returns to normal operation
