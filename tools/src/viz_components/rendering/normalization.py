"""
Data normalization utilities for graph rendering.

Handles normalization of stream data to display coordinates (0-1 range).
"""

import numpy as np


class DataNormalizer:
    """Normalizes stream data to display coordinates"""

    def __init__(self, stream_config, stream_ranges):
        """
        Initialize normalizer.

        Args:
            stream_config: StreamConfigManager instance
            stream_ranges: Dict of {stream_name: (min, max)} for full dataset ranges
        """
        self.stream_config = stream_config
        self.stream_ranges = stream_ranges

    def calculate_axis_owner_range(self, axis_owner, raw_data, view_start, view_end):
        """
        Calculate the display range for the axis owner stream.

        Uses full dataset minimum but visible maximum for dynamic scaling,
        respecting stream configuration constraints (fixed ranges, min_top, etc).

        Args:
            axis_owner: Name of axis owner stream
            raw_data: Dict of raw stream data
            view_start: Start time of visible window
            view_end: End time of visible window

        Returns:
            Tuple of (range_min, range_max) or None if stream not found
        """
        if not axis_owner or axis_owner not in raw_data:
            return None

        # Get full dataset range
        full_min, full_max = self.stream_ranges.get(axis_owner, (0, 1))

        # Get visible data
        stream_data = raw_data[axis_owner]
        all_time = stream_data['time']
        all_values = stream_data['values']
        mask = (all_time >= view_start) & (all_time <= view_end)
        visible_values = all_values[mask]

        # Get stream configuration
        stream_cfg = self.stream_config.get_stream(axis_owner)

        if len(visible_values) > 0:
            # Check for fixed display range (e.g., temperature: 0-120°C)
            if stream_cfg and stream_cfg.display_range_min is not None and stream_cfg.display_range_max is not None:
                # Fixed range - no dynamic scaling
                range_min = stream_cfg.display_range_min
                range_max = stream_cfg.display_range_max
                visible_median = float(np.median(visible_values))
                print(f"Stream {axis_owner}: fixed range [{range_min:.1f}, {range_max:.1f}] (median: {visible_median:.1f})")
            else:
                # Dynamic scaling with optional minimum top constraint
                visible_max = float(visible_values.max())
                range_min = full_min

                # Check for minimum top constraint (e.g., GPS speed: min 30mph at top)
                if stream_cfg and stream_cfg.display_range_min_top is not None:
                    min_top = stream_cfg.display_range_min_top
                    if visible_max < min_top:
                        visible_max = min_top
                        print(f"Stream {axis_owner}: applied min_top constraint {min_top:.1f}")

                range_max = visible_max

            # Avoid division by zero
            if range_max - range_min < 1e-10:
                range_max = range_min + 1.0

            return (range_min, range_max)
        else:
            # No visible values in current view - use constraints or full dataset range
            # Check for fixed display range
            if stream_cfg and stream_cfg.display_range_min is not None and stream_cfg.display_range_max is not None:
                range_min = stream_cfg.display_range_min
                range_max = stream_cfg.display_range_max
                print(f"Stream {axis_owner}: no visible data, using fixed range [{range_min:.1f}, {range_max:.1f}]")
            else:
                # Use full dataset range with optional min_top constraint
                range_min = full_min
                range_max = full_max

                if stream_cfg and stream_cfg.display_range_min_top is not None:
                    min_top = stream_cfg.display_range_min_top
                    if range_max < min_top:
                        range_max = min_top
                        print(f"Stream {axis_owner}: no visible data, using full range with min_top {min_top:.1f}")

            # Avoid division by zero
            if range_max - range_min < 1e-10:
                range_max = range_min + 1.0

            return (range_min, range_max)

    def calculate_stream_range(self, stream_name, raw_data, view_start, view_end,
                              axis_owner_range=None):
        """
        Calculate normalization range for a stream.

        For axis owner: uses provided axis_owner_range
        For other streams: uses full dataset min, visible max

        Args:
            stream_name: Name of stream
            raw_data: Dict of raw stream data
            view_start: Start time of visible window
            view_end: End time of visible window
            axis_owner_range: Pre-calculated range for axis owner (if this is axis owner)

        Returns:
            Tuple of (stream_min, stream_max)
        """
        if axis_owner_range is not None:
            # This is the axis owner - use pre-calculated range
            return axis_owner_range

        # Other streams: full min, visible max
        full_min, full_max = self.stream_ranges.get(stream_name, (0, 1))

        if stream_name not in raw_data:
            return (full_min, full_max)

        stream_data = raw_data[stream_name]
        all_time = stream_data['time']
        all_values = stream_data['values']
        mask = (all_time >= view_start) & (all_time <= view_end)
        visible_values = all_values[mask]

        if len(visible_values) > 0:
            visible_max = float(visible_values.max())
            stream_min = full_min
            stream_max = visible_max
            if stream_max - stream_min < 1e-10:
                stream_max = stream_min + 1.0
        else:
            stream_min, stream_max = full_min, full_max

        return (stream_min, stream_max)

    def normalize_data(self, values, stream_min, stream_max, normalize_max, bar_offset=0.0):
        """
        Normalize data values to display coordinates.

        Args:
            values: Array of data values
            stream_min: Minimum value for normalization
            stream_max: Maximum value for normalization
            normalize_max: Target maximum (typically 0.85)
            bar_offset: Offset to add (for bar space at bottom)

        Returns:
            Normalized array in range [bar_offset, bar_offset + normalize_max]
        """
        normalized = ((values - stream_min) / (stream_max - stream_min)) * normalize_max

        # Shift up by bar offset if bars are enabled
        if bar_offset > 0:
            normalized = normalized + bar_offset

        return normalized
