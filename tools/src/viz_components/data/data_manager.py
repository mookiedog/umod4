"""
Data manager for visualization tool.

Manages loaded data and provides query interface for retrieving
time-windowed data for rendering.
"""

import numpy as np


class DataManager:
    """Manages loaded data and provides query interface"""

    def __init__(self):
        """Initialize empty data manager"""
        self.raw_data = {}
        self.stream_names = []
        self.stream_ranges = {}
        self.stream_metadata = {}
        self.file_metadata = {}
        self.time_min = 0.0
        self.time_max = 0.0
        self.time_span = 0.0

    def set_data(self, raw_data, stream_names, stream_ranges, stream_metadata, file_metadata, time_bounds):
        """
        Set the loaded data.

        Args:
            raw_data: Dict mapping stream names to {'time': array, 'values': array}
            stream_names: List of stream names
            stream_ranges: Dict mapping stream names to (min, max) tuples
            stream_metadata: Dict mapping stream names to metadata dicts
            file_metadata: Dict of HDF5 file attributes
            time_bounds: Tuple of (time_min, time_max)
        """
        self.raw_data = raw_data
        self.stream_names = stream_names
        self.stream_ranges = stream_ranges
        self.stream_metadata = stream_metadata
        self.file_metadata = file_metadata
        self.time_min, self.time_max = time_bounds
        self.time_span = self.time_max - self.time_min

    def clear(self):
        """Clear all loaded data"""
        self.raw_data = {}
        self.stream_names = []
        self.stream_ranges = {}
        self.stream_metadata = {}
        self.file_metadata = {}
        self.time_min = 0.0
        self.time_max = 0.0
        self.time_span = 0.0

    def has_data(self):
        """Check if data is loaded"""
        return len(self.raw_data) > 0

    def get_stream_data(self, stream_name):
        """
        Get full data for a stream.

        Args:
            stream_name: Name of the stream

        Returns:
            Dict with 'time' and 'values' arrays, or None if not found
        """
        return self.raw_data.get(stream_name)

    def get_visible_data(self, stream_name, time_start, time_end):
        """
        Get data for a stream within a time window.

        Args:
            stream_name: Name of the stream
            time_start: Start time (seconds)
            time_end: End time (seconds)

        Returns:
            Tuple of (time_array, values_array) for the visible window,
            or (None, None) if stream not found
        """
        if stream_name not in self.raw_data:
            return None, None

        data = self.raw_data[stream_name]
        time = data['time']
        values = data['values']

        # Filter to time window
        mask = (time >= time_start) & (time <= time_end)
        return time[mask], values[mask]

    def get_stream_range(self, stream_name):
        """
        Get the min/max range for a stream.

        Args:
            stream_name: Name of the stream

        Returns:
            Tuple of (min, max) or (0, 1) if not found
        """
        return self.stream_ranges.get(stream_name, (0.0, 1.0))

    def get_visible_max(self, stream_name, time_start, time_end):
        """
        Get the maximum value for a stream within a time window.

        Args:
            stream_name: Name of the stream
            time_start: Start time (seconds)
            time_end: End time (seconds)

        Returns:
            Maximum value in the window, or None if no data
        """
        time, values = self.get_visible_data(stream_name, time_start, time_end)
        if values is not None and len(values) > 0:
            return float(values.max())
        return None

    def get_visible_min(self, stream_name, time_start, time_end):
        """
        Get the minimum value for a stream within a time window.

        Args:
            stream_name: Name of the stream
            time_start: Start time (seconds)
            time_end: End time (seconds)

        Returns:
            Minimum value in the window, or None if no data
        """
        time, values = self.get_visible_data(stream_name, time_start, time_end)
        if values is not None and len(values) > 0:
            return float(values.min())
        return None

    def get_metadata(self, stream_name):
        """
        Get metadata for a stream.

        Args:
            stream_name: Name of the stream

        Returns:
            Metadata dict or empty dict if not found
        """
        return self.stream_metadata.get(stream_name, {})

    def get_time_bounds(self):
        """
        Get overall time bounds of loaded data.

        Returns:
            Tuple of (time_min, time_max)
        """
        return (self.time_min, self.time_max)

    def calculate_initial_view(self, stream_config, default_duration):
        """
        Calculate initial view window based on configuration.

        Args:
            stream_config: StreamConfigManager instance
            default_duration: Default view duration in seconds

        Returns:
            Tuple of (view_start, view_end)
        """
        nav_anchor = stream_config.get_setting('nav_initial_anchor', 'first_cam')
        nav_offset_before = stream_config.get_setting('nav_offset_before', 1.0)
        nav_offset_after = stream_config.get_setting('nav_offset_after', 5.0)

        if nav_anchor == 'first_cam' and 'ecu_camshaft_timestamp' in self.raw_data:
            # Position around first CAM event
            cam_data = self.raw_data['ecu_camshaft_timestamp']
            if len(cam_data['time']) > 0:
                first_cam_time = float(cam_data['time'][0])
                view_start = max(self.time_min, first_cam_time - nav_offset_before)
                view_end = min(self.time_max, first_cam_time + nav_offset_after)
                return (view_start, view_end)

        # Fall back to data_start
        view_start = self.time_min
        view_duration = min(default_duration, self.time_span * 0.1)
        view_end = min(self.time_max, view_start + view_duration)
        return (view_start, view_end)
