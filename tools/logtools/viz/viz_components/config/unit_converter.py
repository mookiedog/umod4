"""
Unit detection, conversion, and display label generation.

Supports converting between:
- Temperature: Celsius ↔ Fahrenheit
- Velocity: mph ↔ km/h
- Pressure: PSI ↔ bar
- Voltage: volts (display only)
"""


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
        if '_psi' in name_lower:
            return ('pressure', 'psi')
        elif '_bar' in name_lower:
            return ('pressure', 'bar')

        # Voltage detection
        if '_voltage_v' in name_lower or '_v' == name_lower[-2:]:
            return ('voltage', 'volts')

        return (None, None)

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
        Convert pressure between PSI and bar.

        Args:
            value: Pressure value or array
            from_units: 'psi' or 'bar'
            to_units: 'psi' or 'bar'

        Returns:
            Converted value or array
        """
        if from_units == to_units:
            return value

        if from_units == 'psi' and to_units == 'bar':
            return value * 0.0689476
        elif from_units == 'bar' and to_units == 'psi':
            return value / 0.0689476
        else:
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
            if part not in ['c', 'f', 'mph', 'kph', 'psi', 'bar', 'v']:
                readable_parts.append(part.capitalize())

        readable_name = ' '.join(readable_parts)

        # Add unit suffix
        if display_units == 'celsius':
            return f"{readable_name} (°C)"
        elif display_units == 'fahrenheit':
            return f"{readable_name} (°F)"
        elif display_units == 'mph':
            return f"{readable_name} (mph)"
        elif display_units == 'kph':
            return f"{readable_name} (km/h)"
        elif display_units == 'psi':
            return f"{readable_name} (psi)"
        elif display_units == 'bar':
            return f"{readable_name} (bar)"
        elif display_units == 'volts':
            return f"{readable_name} (V)"
        else:
            return readable_name
