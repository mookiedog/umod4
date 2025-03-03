# WP: Wireless Processor

### _Note: As of now, the WP functionality is still speculative: The code is just beginning to be developed!_


The **W**ireless **P**rocessor (**WP** for short) will be responsible for providing the wireless capabilities of the Umod4. It is anticipated that there will be two distinct wireless interfaces:

* Bluetooth: Used as a user interface
    * System configuration and settings
        * Choose default ECU code image selection
        * Set up WiFi credentials
    * ECU code image management
        * Add/Delete ECU code images to an on-board library
    * OTA (Over The Air) firmware updates for WP and EP
* WiFi: Data Transfer
    * Dumping data logs to a local server

The data logs could be dumped via Bluetooth, but the WiFi interface would be much faster.

Possibilities exist for implementing other potential features. These are purely speculative at the moment though:
* Anti-theft Immobilizer: ECU operation could be locked/unlocked with a phone via bluetooth
* Maybe even GPS theft tracking via WiFi or Bluetooth reporting mechanisms

## Risks

The current design uses a Raspberry Pi Pico W board as the hardware basis to implement WP functionality. It remains to be seen if that solution will have enough horsepower to get the job done.