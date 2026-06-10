"""
Sensor conversion utilities for Aprilia motorcycle ECU data.

Contains functions to convert raw ADC values to engineering units.
"""

import math


def ntc_resistance_to_degc(r_ohms):
    """
    Convert NTC resistance in ohms to degrees Celsius using Steinhart-Hart equation.

    Coefficients are calibrated for the Aprilia temperature sensor NTC element,
    derived from measurements at 0°C (5880Ω), 25°C (1992Ω), and 90°C (249Ω).

    Args:
        r_ohms: NTC resistance in ohms

    Returns:
        Temperature in degrees Celsius (float)
    """
    A = 1.142579776e-3
    B = 2.941596847e-4
    C = -0.5305974726e-7
    logR = math.log(r_ohms)
    return (1.0 / (A + (B * logR) + (C * (logR ** 3)))) - 273.15


def convertApriliaTempSensorAdcToDegC(adc):
    """
    Convert Aprilia temperature sensor ADC value to degrees Celsius.

    Uses Steinhart-Hart equation for accurate NTC thermistor conversion.
    The sensor is assumed to be a 2K NTC thermistor at 25°C.

    Args:
        adc: ADC value (0-255, 8-bit)

    Returns:
        Temperature in degrees Celsius (float)
    """
    # Work backwards to get the voltage we must have measured.
    # Vref is nominally 5.0V, and the ADC is 8 bits (255 max value)
    Vref = 5.0
    Vmeas = adc * Vref / 255.0

    # Based on color code, Rtop (R751 or R781) is 2.70K 0.5% (red/violet/black/brown/green).
    # It measures out at 2.70K, so that is accurate.
    Rtop = 2700

    # Work out what the resistance of the thermistor must have been to generate the Voltage we measured
    Rntc = (Vmeas * Rtop) / (Vref - Vmeas)

    return ntc_resistance_to_degc(Rntc)


def convertPressureSensorAdcToKpa(adc_counts):
    """
    Convert Aprilia MAP/AAP pressure sensor ADC counts to kPa.

    Sensor output formula: Vo = Vcc * (0.006*Pi + 0.12)
    Solving for pressure: Pi = [Vo - (0.12*Vcc)] / (0.006*Vcc)
    Where Vo = (ADC/256) * Vref
    and Vref = Vcc = 5.0V

    Args:
        adc_counts: ADC value (0-255, 8-bit)

    Returns:
        Pressure in kPa (float)
    """
    Vref = 5.0
    Vo = (adc_counts / 256.0) * Vref
    pressure_kpa = (Vo - (0.12 * Vref)) / (0.006 * Vref)
    return pressure_kpa
