WiFi.md

# Umod4 WiFi Interface

The umod4 has a built-in wifi interface to allow all kinds of wireless operations.

## Getting Started

A umod4 device wants to connect to your home network so it can contact the server to upload ride logs, or let you install firmware updates wirelessly.
Fresh out of the box, the umod4 does not know the name of your home network (its "SSID"), or its password.
In this circumstance, the umod4 boots into a special AP (Access Point) mode where it creates its own wifi network.
In this mode, you would connect to the umod4's network with your phone and then enter your real home network's credentials.
After entering the credentials, the umod4 will reboot and connect to the home network from then on.
Note that umod4 wifi interface does have a button you can click that erases all wifi credentials.
If you click that button, the umod4 will go back to AP mode and require you to enter new credentials before it will connect to a home network again.

### Connecting to Umod4 in AP Mode

Use your phone wifi connections mechanism to scan for networks in your area.
The umod4 will create a network of the form "umod4_XXXX", where XXXX are the last four digits of your specific umod4's macaddr.
For example, my Tuono creates a network called "umod4_3BFF".
Since macaddrs never change, I can always tell it apart from the ECU on my testbench which creates a network called "umod4_1E9B".

Connect your phone to your own "umod4_XXXX" network.
When the wifi connection process asks for a password, the default password will be identical to the SSID name, so "umod4_3BFF" for my Tuono.

### Navigate to Web Interface in AP mode

In AP mode, the umod4's web interface will always be at IP address "192.168.4.1".
Open any web browser on your phone, and type 192.168.4.1 into the address bar.
You will see the home page of the

## Umod4 Web Interface

The umod4 board implements a simple web server