# WP: Wireless Processor

### _Note: As of now, a lot of the WP functionality is still speculative: The code is under active development!_

The **W**ireless **P**rocessor (**WP** for short) has a few jobs.
It is resonsible for receiving two separate data streams: the ECU data stream forwarded by the EP, and a GPS position and velocity data stream from a GPS module.
The WP merges the two data streams in a time-correlated fashion, and writes the resulting log to a micro SD card.

In the future, the WP will finally add wireless capabilities to the system.
Wireless would be used to do things like:

* automatic upload of ride logs
* OTA (Over The Air) software updates for all the processors in the system

It is anticipated that there will be two distinct wireless interfaces, WiFi and Bluetooth:

* Bluetooth: Used for a user interface for system control
  * System configuration and settings
    * Choose default ECU code image selection
    * Set up WiFi credentials
  * ECU code image management
    * Select what image to run before a ride
    * Add/Delete ECU code images to an on-board library
* WiFi: for large scale data movement
  * Dumping data logs to a local server
  * Installing OTA firmware updates

The data logs could be dumped via Bluetooth, but the WiFi interface would be much faster.

## Risks

The current design uses a Raspberry Pi Pico 2 W board as the hardware basis to implement WP functionality. It remains to be seen if that solution will have enough horsepower to get the job done.