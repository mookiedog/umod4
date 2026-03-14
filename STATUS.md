# Project Status

In the grand scheme of things, the umod4 project has a number of significant pieces to it:

1) PCB design **WORKS**
    * Implements what looks like an EPROM that plugs into the ECU's EPROM socket
    * Adds SD Card interface, GPS module, wireless interface module
    * Version 4V1 has been in use for 1 year now
    * Version 4V2 is under development. There is no timeline for 4V2 release since 4V1 has no showstopper problems at the moment.

1) UM4 Datalogging firmware for the ECU **WORKS**
    * Modifies stock firmware to emit a data stream of "interesting" ECU data to another processor in real time
    * The set of "interesting data" is certainly not complete
        * One long-term goal is to have enough data collected of the proper kind to enable displaying precisely what parts of the ECU maps were in use to calculate the fuel and ignition events for each rotation of the crankshaft.

1) EP (Eprom Processor) **WORKS**
    * Constructs an EPROM image to present to the ECU
    * Performs the duties of both EPROM and RAM
    * Add a communication sidechannel for receiving the incoming ECU data stream
    * Captured ECU data stream gets forwarded to the WP for logging

1) WP (Wireless Processor) **WORKS**
    * Merges the incoming GPS and ECU data streams into a time-correlated log stored on SD card
    * Updates the EP software as well as its own WP software using WiFi
        * EP software is updated using the SWD Debug interface. This essentially makes the EP 'unbrickable": the WP is always able to flash new firmware into it.
    * Implements a simple control website for:
        * Selecting what EPROM image should be run by default
        * Manipulating the files in the WP's SD Card filesystem
        * Viewing a real time ECU data stream

        The control website is designed to be accessible even when the WP is not connected to its home network.
        Under those conditions, the WP will automatically go into AP (Access Point) mode and generate its own WiFi network.
        Connecting a phone to this network will always allow a user to control the umod4 in situations without cel coverage or access to the home network.

1) Server App **WORKS**
    * Automatically unloads ride logs from the bike and/or a test ECU to a PC when the target is connected to WiFi
        * The server can handle multiple bikes each with their own umod4 board
        * logs are stored by bike, by date, and by log number
        * logs contain enough information to know exactly what EPROMs and tables were in use for a ride, and exactly when/where the ride happened via GPS data
    * Provides the mechanism to push software updates into the WP or EP
    * Provides an interface to look at the filesystem on the WP, and upload/download/delete files as a user desires

1) Log Decoder App **WORKS**
    * Converts the highly compressed internal version of the logfiles into two forms:
        * a form meant for human examination and debug purposes
        * a form that optimizes the log data for graphing when used by the Visualizer

1) Visualizer App **WORKS**
    * Displays the log data in a time-based fashion that allows a user to look at various ECU data streams over a period of time ranging from the entire ride down to partial rotations of the crankshaft at redline speeds
    * Correlates location and velocity with the ECU data stream
        * can generate ride views overlayed on Google Maps

1) Control Website **WORKS**

    Allows a user to use their phone to:
    * Upload/delete EPROM images into EP image store
    * Select what ECU software they would like to use for a ride
    * Set up WiFi credentials
    * Manipulate files on the WP filesystem

1) Tuning App **DEFERRED**

    *There could be value in building a complete tuning app into the umod4 ecosystem, but for now, external tools exist and should be used.*

    Users can create their own tuned images using tools like TunerPro.
    The tuned EPROM images get uploaded from a user's PC to the umod4's image store via wifi.
    Multiple images can be uploaded to support running a number of experiments on a single ride.

    A user would use the image selector mechanism built into the umod4's control website to select what image would run by default at the next ignition-on event.
