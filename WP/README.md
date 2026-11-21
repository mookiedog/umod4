# WP: Wireless Processor

The **W**ireless **P**rocessor (**WP** for short) is a Raspberry Pi Pico 2 W (an RP2350) running FreeRTOS.
It is the umod4's logger and its network interface.

Its jobs:

* **Logging.**
  It receives the ECU data stream forwarded by the EP, and a GPS position and velocity stream from a uBlox GPS module.
  It merges the two streams in a time-correlated fashion and writes the log to a micro SD card.
* **WiFi.**
  It serves the umod4's web pages and lets the PC server download new logs after a ride.
  It joins your home network, or provides its own hotspot if it cannot find yours.
* **Reflashing.**
  It can update its own firmware over WiFi, and it reflashes the EP over the EP's SWD interface.
  All firmware updates go through the WP for that reason.

How it all fits together is in [HOW_IT_WORKS.md](../HOW_IT_WORKS.md#wp-operation).
What a rider does with it is in the [User Guide](../USER_GUIDE.md).

## Source layout

* `src/` has the firmware.
* `www/` has the web pages the WP serves.
  The build turns them into C data with `tools/makefsdata`.
* `partition_table.json` describes the layout of the WP's flash.
