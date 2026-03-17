"""
log_convert.py — Sensor conversion functions for Aprilia ECU log data.

Converts raw ADC values from the ECU data stream to engineering units.
Function names intentionally mirror the naming convention used in the C tooling.

Previously these conversions existed in two places:
  - tools/logtools/decoder/conversions.py  (temperature, pressure as functions)
  - tools/logtools/decoder/decodelog.py    (VM and ignition advance as inline code)
This module is the single canonical source.
"""

import math


def logconv_ecu_raw_thw(adc):
    """Convert coolant or air temperature sensor ADC value (0–255) to degrees Celsius.

    Uses the Steinhart-Hart equation for an NTC thermistor.
    Circuit: Rtop = 2.70 kΩ (R751/R781), Vref = 5.0 V, 8-bit ADC.

    Steinhart-Hart coefficients derived from measurements at 0 °C, 25 °C, and 90 °C:
        A = 1.142579776e-3
        B = 2.941596847e-4
        C = -0.5305974726e-7
    """
    Vref = 5.0
    Vmeas = adc * Vref / 255.0

    # Resistance of the NTC thermistor
    Rtop = 2700
    if Vref - Vmeas == 0:
        return float('nan')
    Rntc = (Vmeas * Rtop) / (Vref - Vmeas)

    # Steinhart-Hart (more accurate than Beta method; differs by ~1 °C at most)
    A = 1.142579776e-3   # 5880 Ω at 0 °C
    B = 2.941596847e-4   # 1992 Ω at 25 °C
    C = -0.5305974726e-7 #  249 Ω at 90 °C
    logR = math.log(Rntc)
    return (1.0 / (A + B * logR + C * logR ** 3)) - 273.15


# THA uses the same sensor and formula as THW
logconv_ecu_raw_tha = logconv_ecu_raw_thw


def logconv_ecu_raw_map(adc):
    """Convert MAP/AAP pressure sensor ADC value (0–255) to kPa.

    Sensor output formula: Vo = Vcc * (0.006 * Pi + 0.12)
    Solving for Pi: Pi = (Vo - 0.6) / 0.03
    Where Vo = (ADC / 256) * Vref and Vref = Vcc = 5.0 V.
    """
    Vref = 5.0
    Vo = (adc / 256.0) * Vref
    return (Vo - (0.12 * Vref)) / (0.006 * Vref)


# AAP uses the same sensor and formula as MAP
logconv_ecu_raw_aap = logconv_ecu_raw_map


def logconv_ecu_raw_vm(adc):
    """Convert battery voltage monitor ADC value (0–255) to volts.

    The VM circuit divides the battery voltage by 4 via a resistor divider,
    then feeds it to an 8-bit ADC where 5 V → 0xFF (256 counts).
    """
    return (adc / 256.0) * 5.0 * 4.0


def logconv_ecu_ign_dly(raw):
    """Convert 0.8 fixed-point ignition delay byte to ignition advance in degrees.

    The ECU stores ignition timing as a fraction of 90 degrees, offset by −18°.
    Formula: advance = (raw / 256) * 90 − 18
    """
    return (raw / 256.0) * 90.0 - 18.0


def logconv_ecu_raw_vta(raw_u16):
    """Extract throttle position ADC value from the 16-bit VTA word.

    The upper 6 bits carry timer information; the lower 10 bits are the ADC value.
    Returns an integer in the range 0–1023.
    """
    return raw_u16 & 0x3FF


# ---------------------------------------------------------------------------
# Backwards-compatibility aliases matching the old conversions.py names.
# New code should use the logconv_* names above.
convertApriliaTempSensorAdcToDegC = logconv_ecu_raw_thw
convertPressureSensorAdcToKpa     = logconv_ecu_raw_map
