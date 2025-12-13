"""
Application configuration management using Qt QSettings.

Handles persistent storage of:
- Window geometry and state
- Recently opened files
- User display preferences (theme, unit conversions)
- Splitter positions
- Stream order
"""

import os
from PySide6.QtCore import QSettings, QByteArray


class AppConfig:
    """
    Global application configuration manager using QSettings.

    Handles:
    - Window geometry and state
    - Recently opened files
    - User display preferences (theme, unit conversions)
    - Splitter positions
    """

    def __init__(self):
        # Use organization and application name for proper scoping
        # This will create: ~/.config/umod4/LogVisualizer.conf on Linux
        self.settings = QSettings("umod4", "LogVisualizer")

        # Default values
        self.defaults = {
            # Application settings
            "theme": "light",
            "max_recent_files": 10,
            "default_view_duration": 10.0,  # seconds
            "show_grid": True,
            "grid_alpha": 0.3,
            "max_plot_points": 5000,
            "default_file_location": str(os.path.expanduser("~/logs")),
            "axis_font_size": 12,  # Font size for all axis labels and tick labels

            # Display unit preferences (global, apply to all files)
            "temperature_units": "celsius",  # "celsius" or "fahrenheit"
            "velocity_units": "mph",         # "mph" or "kph"
            "pressure_units": "psi",         # "psi" or "bar"
            "distance_units": "miles",       # "miles" or "km"
        }

    def get(self, key, default=None):
        """Get a configuration value with optional default."""
        if default is None and key in self.defaults:
            default = self.defaults[key]

        # Type-safe retrieval based on key type
        if isinstance(default, bool):
            return self.settings.value(key, default, type=bool)
        elif isinstance(default, int):
            return self.settings.value(key, default, type=int)
        elif isinstance(default, float):
            return self.settings.value(key, default, type=float)
        else:
            return self.settings.value(key, default, type=str)

    def set(self, key, value):
        """Set a configuration value."""
        self.settings.setValue(key, value)
        self.settings.sync()  # Force write to disk

    # Unit preference accessors
    def get_temperature_units(self):
        """Get preferred temperature units."""
        return self.get("temperature_units")

    def set_temperature_units(self, units):
        """Set preferred temperature units ('celsius' or 'fahrenheit')."""
        self.set("temperature_units", units)

    def get_velocity_units(self):
        """Get preferred velocity units."""
        return self.get("velocity_units")

    def set_velocity_units(self, units):
        """Set preferred velocity units ('mph' or 'kph')."""
        self.set("velocity_units", units)

    def get_pressure_units(self):
        """Get preferred pressure units."""
        return self.get("pressure_units")

    def set_pressure_units(self, units):
        """Set preferred pressure units ('psi' or 'bar')."""
        self.set("pressure_units", units)

    # Recent files management
    def get_recent_files(self):
        """Get list of recently opened files."""
        files = self.settings.value("recent_files", [], type=list)
        # Filter out non-existent files
        return [f for f in files if os.path.exists(f)]

    def add_recent_file(self, filepath):
        """Add a file to the recent files list."""
        filepath = os.path.abspath(filepath)
        recent = self.get_recent_files()

        # Remove if already exists (will be re-added at top)
        if filepath in recent:
            recent.remove(filepath)

        # Add to beginning
        recent.insert(0, filepath)

        # Limit to max count
        max_count = self.get("max_recent_files")
        recent = recent[:max_count]

        self.settings.setValue("recent_files", recent)
        self.settings.sync()

    def clear_recent_files(self):
        """Clear the recent files list."""
        self.settings.setValue("recent_files", [])
        self.settings.sync()

    # Window geometry
    def save_window_geometry(self, window):
        """Save window geometry and state."""
        self.settings.beginGroup("MainWindow")
        self.settings.setValue("geometry", window.saveGeometry())
        self.settings.setValue("state", window.saveState())
        self.settings.endGroup()
        self.settings.sync()

    def restore_window_geometry(self, window):
        """Restore window geometry and state."""
        self.settings.beginGroup("MainWindow")
        geometry = self.settings.value("geometry", QByteArray())
        state = self.settings.value("state", QByteArray())
        self.settings.endGroup()

        if geometry:
            window.restoreGeometry(geometry)
        if state:
            window.restoreState(state)

    # Splitter positions
    def save_splitter_state(self, name, splitter):
        """Save splitter sizes."""
        self.settings.beginGroup("Splitters")
        self.settings.setValue(name, splitter.saveState())
        self.settings.endGroup()
        self.settings.sync()

    def restore_splitter_state(self, name, splitter):
        """Restore splitter sizes."""
        self.settings.beginGroup("Splitters")
        state = self.settings.value(name, QByteArray())
        self.settings.endGroup()

        if state:
            splitter.restoreState(state)

    # Stream order persistence
    def save_stream_order(self, stream_order):
        """Save the custom stream order."""
        self.settings.setValue("stream_order", stream_order)
        self.settings.sync()

    def get_stream_order(self):
        """Get the saved stream order."""
        return self.settings.value("stream_order", [], type=list)
