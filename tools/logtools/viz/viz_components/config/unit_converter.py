"""
Unit detection, conversion, and display label generation.

Supports converting between:
- Temperature: Celsius <-> Fahrenheit
- Velocity: mph <-> km/h
- Pressure: PSI <-> bar
- Throttle: ADC counts <-> % open
- Voltage: volts (display only)
"""

import numpy as np


class UnitConverter:
    """
    Handles unit detection, conversion, and display label generation.
    """

    @staticmethod
    def parse_units_from_name(dataset_name):
        """
        Extract native units from dataset name based on suffix.

        Args:
            dataset_name: Name of the dataset (e.g., 'ecu_coolant_temp_c')

        Returns:
            Tuple of (data_type, native_units) or (None, None) if unknown
        """
        name_lower = dataset_name.lower()

        # Throttle position detection (must come before generic suffix checks)
        if 'throttle_adc' in name_lower:
            return ('throttle', 'adc')

        # Temperature detection
        if '_temp_c' in name_lower or 'coolant_temp_c' in name_lower or 'air_temp_c' in name_lower:
            return ('temperature', 'celsius')
        elif '_temp_f' in name_lower:
            return ('temperature', 'fahrenheit')

        # Velocity detection
        if '_velocity_mph' in name_lower or '_speed_mph' in name_lower:
            return ('velocity', 'mph')
        elif '_velocity_kph' in name_lower or '_speed_kph' in name_lower:
            return ('velocity', 'kph')

        # Pressure detection
        if '_kpa' in name_lower:
            return ('pressure', 'kpa')
        elif '_psi' in name_lower:
            return ('pressure', 'psi')
        elif '_bar' in name_lower:
            return ('pressure', 'bar')

        # Voltage detection
        if '_voltage_v' in name_lower or '_v' == name_lower[-2:]:
            return ('voltage', 'volts')

        # Data rate detection
        if '_bps' in name_lower or '_data_rate' in name_lower:
            return ('data_rate', 'bytes_per_sec')

        return (None, None)

    @staticmethod
    def get_conversion_options(data_type):
        """
        Return list of (label, units_key) pairs for the right-click unit menu.
        Only data types with multiple display options are listed.

        Args:
            data_type: Type string from parse_units_from_name()

        Returns:
            List of (label, units_key) tuples, or empty list if no options
        """
        options = {
            'throttle':    [('% Open', 'percent_open'), ('ADC', 'adc')],
            'temperature': [('deg C', 'celsius'), ('deg F', 'fahrenheit')],
            'velocity':    [('MPH', 'mph'), ('KPH', 'kph')],
            'pressure':    [('kPa', 'kpa'), ('PSI', 'psi')],
        }
        return options.get(data_type, [])

    @staticmethod
    def convert(value, data_type, from_units, to_units):
        """Dispatch conversion by data type."""
        if data_type == 'temperature':
            return UnitConverter.convert_temperature(value, from_units, to_units)
        elif data_type == 'velocity':
            return UnitConverter.convert_velocity(value, from_units, to_units)
        elif data_type == 'pressure':
            return UnitConverter.convert_pressure(value, from_units, to_units)
        elif data_type == 'throttle':
            return UnitConverter.convert_throttle(value, from_units, to_units)
        return value

    @staticmethod
    def convert_temperature(value, from_units, to_units):
        """
        Convert temperature between Celsius and Fahrenheit.

        Args:
            value: Temperature value or array
            from_units: 'celsius' or 'fahrenheit'
            to_units: 'celsius' or 'fahrenheit'

        Returns:
            Converted value or array
        """
        if from_units == to_units:
            return value

        if from_units == 'celsius' and to_units == 'fahrenheit':
            return (value * 9.0/5.0) + 32.0
        elif from_units == 'fahrenheit' and to_units == 'celsius':
            return (value - 32.0) * 5.0/9.0
        else:
            return value  # Unknown conversion

    @staticmethod
    def convert_velocity(value, from_units, to_units):
        """
        Convert velocity between MPH and km/h.

        Args:
            value: Velocity value or array
            from_units: 'mph' or 'kph'
            to_units: 'mph' or 'kph'

        Returns:
            Converted value or array
        """
        if from_units == to_units:
            return value

        if from_units == 'mph' and to_units == 'kph':
            return value * 1.60934
        elif from_units == 'kph' and to_units == 'mph':
            return value / 1.60934
        else:
            return value

    @staticmethod
    def convert_pressure(value, from_units, to_units):
        """
        Convert pressure between kPa, PSI, and bar.

        Args:
            value: Pressure value or array
            from_units: 'kpa', 'psi', or 'bar'
            to_units: 'kpa', 'psi', or 'bar'

        Returns:
            Converted value or array
        """
        if from_units == to_units:
            return value

        if from_units == 'kpa' and to_units == 'psi':
            return value * 0.145038
        elif from_units == 'psi' and to_units == 'kpa':
            return value / 0.145038
        elif from_units == 'kpa' and to_units == 'bar':
            return value * 0.01
        elif from_units == 'bar' and to_units == 'kpa':
            return value * 100.0
        elif from_units == 'psi' and to_units == 'bar':
            return value * 0.0689476
        elif from_units == 'bar' and to_units == 'psi':
            return value / 0.0689476
        else:
            return value

    # TPS calibration constants (see ecu/doc/TPS_OPERATION.md)
    # 0% open: center of the ECU calibration acceptance range (128..132 ADC counts)
    # 100% open: measured full-open ADC value
    TPS_ADC_CLOSED = 130
    TPS_ADC_OPEN   = 799

    @staticmethod
    def convert_throttle(value, from_units, to_units):
        """
        Convert throttle position between raw ADC counts and % open.

        Ratiometric formula (ecu/doc/TPS_OPERATION.md, "The Conversion Formula"):
          pct = (ADC - TPS_ADC_CLOSED) / (TPS_ADC_OPEN - TPS_ADC_CLOSED) * 100
        Values slightly below TPS_ADC_CLOSED will yield small negative percentages;
        this is intentional (better than clamping, preserves real sensor data).
        ADC values 0 and 1023 are sensor fault sentinels -> NaN in % mode.
        """
        if from_units == to_units:
            return value
        span = UnitConverter.TPS_ADC_OPEN - UnitConverter.TPS_ADC_CLOSED
        if from_units == 'adc' and to_units == 'percent_open':
            arr = np.asarray(value, dtype=float)
            fault = (arr == 0) | (arr == 1023)
            pct = (arr - UnitConverter.TPS_ADC_CLOSED) / span * 100.0
            pct[fault] = np.nan
            return pct if np.ndim(value) > 0 else float(pct)
        if from_units == 'percent_open' and to_units == 'adc':
            arr = np.asarray(value, dtype=float)
            adc = arr / 100.0 * span + UnitConverter.TPS_ADC_CLOSED
            adc[np.isnan(arr)] = 0
            return np.round(adc).astype(int) if np.ndim(value) > 0 else int(round(float(adc)))
        return value

    @staticmethod
    def get_display_name(dataset_name, native_units, display_units):
        """
        Generate a human-readable display name with units.

        Args:
            dataset_name: Original dataset name
            native_units: Native units from dataset
            display_units: User's preferred display units

        Returns:
            String like "Coolant Temperature (°F)" or "GPS Velocity (km/h)"
        """
        # Convert dataset name to readable format
        # e.g., 'ecu_coolant_temp_c' -> 'Coolant Temp'
        parts = dataset_name.replace('ecu_', '').replace('_', ' ').split()

        # Remove unit suffixes from name
        readable_parts = []
        for part in parts:
            if part not in ['c', 'f', 'mph', 'kph', 'psi', 'bar', 'kpa', 'v', 'bps', 'adc']:
                readable_parts.append(part.capitalize())

        readable_name = ' '.join(readable_parts)

        # Add unit suffix
        if display_units == 'celsius':
            return f"{readable_name} (deg C)"
        elif display_units == 'fahrenheit':
            return f"{readable_name} (deg F)"
        elif display_units == 'mph':
            return f"{readable_name} (mph)"
        elif display_units == 'kph':
            return f"{readable_name} (km/h)"
        elif display_units == 'kpa':
            return f"{readable_name} (kPa)"
        elif display_units == 'psi':
            return f"{readable_name} (psi)"
        elif display_units == 'bar':
            return f"{readable_name} (bar)"
        elif display_units == 'volts':
            return f"{readable_name} (V)"
        elif display_units == 'bytes_per_sec':
            return f"{readable_name} (B/s)"
        elif display_units == 'percent_open':
            return f"{readable_name} (% open)"
        elif display_units == 'adc':
            return f"{readable_name} (ADC)"
        else:
            return readable_name
