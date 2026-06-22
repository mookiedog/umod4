# Bench Testing

A bench test setup is used for software developement and also by the automated test harness.
In short, it is a standalone ECU with a subset of the engine sensors available to it.

## Limitations

It would be nice to mount coils/sparkplugs and a power supply to drive them, but I don't have the means of simulating a rotating engine.
Previous experiments have shown that simulating crank and cam sensors is a real problem.
Until I have a solution for that, it is a waste of time to even think about mounting coils and plugs.

## Powering

A bench supply is used to power everything.
They have current reporting and current limiting, which are very important features!

## Built-In Sensors

The following sensors are built into the ECU:

* AAP (Atmospheric Air Pressure)
* TP0/1 (Trim Pots)
* VM (Voltage Monitor of main +BAT supply)
  * Note that VM needs a jumper wire to connect it to VBAT

## External Sensors

The bench setup adds the following sensors as external components to the ECU:

* THW (Coolant Temperature)
* THA (Air Temperature)
* TPS (Throttle Position)

### THW

Coolant temperature is simulated with a 470 Ohm resistor to GND.
This corresponds to an expected temperature of 67.8°C.
Given a nominal ±5% resistor accuracy, the acceptable range of reported temps would be 65.4 to 68.7°C.

### THA

Ambient air temperature is simulated with a 2000 Ohm resistor to GND.
This corresponds to an expected temperature of 25.5°C.
Given a nominal ±5% resistor accuracy, the acceptable range of reported temps would be 23.7 to 26.2°C.

### TPS

The TPS sensor is simulated with a resistor divider with a pot on the bottom resistor to allow for fine adjustments.
The point of the adjustment is to support the TPS calibration mechanisms.
The adjustable pot allows a user to dial in the desired ADC output of 128..132 ADC counts to represent the TPS output at the fully closed position.
The wiring of the pot is such that a knob on the pot should be rotated in the same direction as the TPS graphic on the WP's calibration webpage.
```
      Vin
      ┌┴┐
      │ │  R_top (3K Ohm)
      └┬┘
       ├─── Vout
      ┌┴┐
      │ │  R_bottom (390 Ohm)
      └┬┘
       │
      ┌┴┐  R_bottom_adj (100 Ohm)
      │ │◄──┐
      └┬┘   │
      (NC)  │
           ─┴─ GND
```

## Wiring

Note: the easiest way to power everything is to obtain an original mating connecter from a scrapped wiring harness.

Failing that, the ECU can be powered via an AC adapter socket using a home-made bracket on the circuit board, as shown below:

![image](doc/images/bench_power.jpg)

Follow the directions below to connect up power and ground.

### Power

The AC adapter I used has a center-positive pin.
The red wire in the picture represents the positive input voltage.
You can see that it gets soldered directly to the B+ connector pin on the main ECU connector.

### Ground

There are five ground wires:

* E1/E2: electronics ground
* E01/E02: injector driver ground
* E03: ignition coil driver ground

Note that E1/E2 and E01/E02 are all connected together on the PCB itself.
This means that the single black wire in the picture above is actually connecting to all four E1/E2/E01/E02 pins.

For bench purposes, E03 needs its own GND connection.
In the picture above, you can see a tiny wire stub that connects E01 to E03.

__Warning: An ECU wired up like this should not be placed in service on a motorbike!__

## Current Consumption

In normal conditions (no WiFi), the ECU draws around 190 mA.
The average current when WiFi is active is more like 240 to 250 mA.
The peak power consumption with absolutely everyting installed (SD card present and writing, GPS searching for satellites, WiFi operations in progress) might be as high as 400 mA for short durations.

For a bit of margin, the bench supply or AC adapter should be capable of supplying 1A.

## Details

### Battery Input: B+

This wire gets battery power from the wiring harness whenever the ignition is turned ON.

The ECU's linear regulator was measured as needing a 0.9V of headroom, meaning that it needs at least 5.9V on its B+ input to ensure that the computer circuitry inside operates at its 5.0V design target.

I could power the test bench ECU at a nominal 12V to 14V input, but that would do nothing except make the ECU's 5V regulator work harder to get rid of the extra input voltage as heat.
When powering my system from an adjustable power supply, I run it at about 8 Volts.
I can also run it from a 9VDC "wall wart" power adaptor.

### Grounds

As mentioned earlier, the ECU has a five separate ground connections.

One reason for this is that when the ECU switches the relatively high power ignition coil loads, they can generate large electrical transients on their GND wires.
The motorbike wiring harness keeps the ECU computer ground path separate from the ignition coil ground path.
This helps keep the coil switching noise from disturbing the computer and analog circuitry inside the ECU.

When powering the ECU using a bench power supply with no ignition coils attached, all 5 GNDs can be wired together for simplicity.

#### E1/E2 & E01/E02

All 4 of these connector pins are tied to the same GND on the circuit board.

#### E03

This is the GND for the ignition coil driver transistors.
For a bench setup, it is fine to connect E03 to the same power supply GND as the ECU uses.

## Mechanical

The screws used to mount the PCB on the bottom place are M3 x 0.5mm pitch.
That's the same as the screws that hold the cover to the bottom plate.

## Goals

* Big power supply for coils
* Power options for ECU:
  * From big supply
  * from bench supply
