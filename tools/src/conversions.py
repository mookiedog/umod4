"""
Sensor conversion utilities for Aprilia motorcycle ECU data.

Contains functions to convert raw ADC values to engineering units.
"""

import math


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

    # Convert an NTC resistor to a temperature using the Beta method
    #
    # The Beta constant was calculated from the ntccalculator website
    # The resistances for the Beta calculation came from measurements of sensor resistances at 0C, 25C, and 90C.
    # Assumption: the NTC resistor in the Aprilia sensor is rated 2K Ohms at 25C.
    # Measured at 1992 Ohms at 25C. 2K is a standard NTC value, so this seems reasonable.
    R25 = 1992
    Beta = 3526
    degC_Beta = (1/((1/Beta)*math.log(Rntc/R25)+(1/(25+273.15))))-273.15

    # The Steinhart-Hart coefficients come from the same calculator website
    # Resistance/temperature Measurements come from experiment:
    A = 1.142579776e-3          # 5880 Ohms at 0C
    B = 2.941596847e-4          # 1992 Ohms at 25C
    C = -0.5305974726e-7        #  249 Ohms at 90C
    logR = math.log(Rntc)
    degC_SH = (1/(A + (B * logR) + (C * (logR**3)))) - 273.15

    # Experiments show that S-H and Beta differ by about 1 degree at most.
    # The S-H method is known to be more accurate so we use it.
    return degC_SH


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
