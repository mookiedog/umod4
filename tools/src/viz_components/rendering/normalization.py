"""
Data normalization utilities for graph rendering.

Handles normalization of stream data to display coordinates (0-1 range).
"""

import numpy as np


class DataNormalizer:
    """
    Normalizes stream data to display coordinates (0-1 range).

    This class centralizes all range calculation and normalization logic,
    applying display constraints from YAML configuration:

    - Fixed ranges (display_range_min/max): e.g., temperatures always 0-120°C
    - Minimum top constraints (display_range_min_top): e.g., GPS speed min 30mph at top
    - Maximum bottom constraints (display_range_max_bottom): e.g., AAP max 80kPa at bottom
    - Dynamic scaling: Uses full dataset min, visible data max (default behavior)

    All streams (whether they own the left axis or display on the right) use
    the same unified calculation method for consistency.
    """

    def __init__(self, stream_config, stream_ranges, debug=False):
        """
        Initialize normalizer.

        Args:
            stream_config: StreamConfigManager instance
            stream_ranges: Dict of {stream_name: (min, max)} for full dataset ranges
            debug: Enable debug output (default: False)
        """
        self.stream_config = stream_config
        self.stream_ranges = stream_ranges
        self.debug = debug

    def debug_print(self, *args, **kwargs):
        """Print debug message if debug flag is enabled"""
        if self.debug:
            print(*args, **kwargs)

    def calculate_axis_owner_range(self, axis_owner, raw_data, view_start, view_end):
        """
        Calculate the display range for the axis owner stream.

        Uses full dataset minimum but visible maximum for dynamic scaling,
        respecting stream configuration constraints (fixed ranges, min_top, etc).

        This is a convenience wrapper around calculate_stream_range() for backward compatibility.

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

        # Use the unified calculate_stream_range method
        return self.calculate_stream_range(axis_owner, raw_data, view_start, view_end, axis_owner_range=None)

    def calculate_stream_range(self, stream_name, raw_data, view_start, view_end,
                              axis_owner_range=None):
        """
        Calculate normalization range for a stream with display constraints applied.

        This is the unified method for calculating display ranges for all streams,
        whether they own an axis or not. It applies display constraints from the
        YAML configuration (fixed ranges, min_top constraints, etc.).

        Args:
            stream_name: Name of stream to calculate range for
            raw_data: Dict of raw stream data
            view_start: Start time of visible window
            view_end: End time of visible window
            axis_owner_range: Pre-calculated range for axis owner (optimization to avoid
                            recalculation when this stream IS the axis owner)

        Returns:
            Tuple of (stream_min, stream_max) with constraints applied
        """
        if axis_owner_range is not None:
            # This is the axis owner and range was pre-calculated - use it directly
            return axis_owner_range

        # Calculate range with constraints applied
        full_min, full_max = self.stream_ranges.get(stream_name, (0, 1))

        if stream_name not in raw_data:
            return (full_min, full_max)

        stream_data = raw_data[stream_name]
        all_time = stream_data['time']
        all_values = stream_data['values']
        mask = (all_time >= view_start) & (all_time <= view_end)
        visible_values = all_values[mask]

        # Get stream configuration for display constraints
        stream_cfg = self.stream_config.get_stream(stream_name)

        if len(visible_values) > 0:
            # Check for fixed display range (e.g., temperature: 0-120°C)
            if stream_cfg and stream_cfg.display_range_min is not None and stream_cfg.display_range_max is not None:
                stream_min = stream_cfg.display_range_min
                stream_max = stream_cfg.display_range_max
                visible_median = float(np.median(visible_values))
                self.debug_print(f"Stream {stream_name}: fixed range [{stream_min:.1f}, {stream_max:.1f}] (median: {visible_median:.1f})")
            else:
                # Dynamic scaling with optional constraints
                visible_min = float(visible_values.min())
                visible_max = float(visible_values.max())
                stream_min = full_min
                stream_max = visible_max

                # Check for minimum top constraint (e.g., GPS speed: min 30mph at top)
                if stream_cfg and stream_cfg.display_range_min_top is not None:
                    min_top = stream_cfg.display_range_min_top
                    if stream_max < min_top:
                        stream_max = min_top
                        self.debug_print(f"Stream {stream_name}: applied min_top constraint {min_top:.1f}")

                # Check for maximum bottom constraint (e.g., AAP: max 80kPa at bottom)
                if stream_cfg and stream_cfg.display_range_max_bottom is not None:
                    max_bottom = stream_cfg.display_range_max_bottom
                    if stream_min > max_bottom:
                        stream_min = max_bottom
                        self.debug_print(f"Stream {stream_name}: applied max_bottom constraint {max_bottom:.1f}")

            if stream_max - stream_min < 1e-10:
                stream_max = stream_min + 1.0
        else:
            # No visible values - use constraints or full dataset range
            if stream_cfg and stream_cfg.display_range_min is not None and stream_cfg.display_range_max is not None:
                stream_min = stream_cfg.display_range_min
                stream_max = stream_cfg.display_range_max
                self.debug_print(f"Stream {stream_name}: no visible data, using fixed range [{stream_min:.1f}, {stream_max:.1f}]")
            else:
                stream_min = full_min
                stream_max = full_max

                if stream_cfg and stream_cfg.display_range_min_top is not None:
                    min_top = stream_cfg.display_range_min_top
                    if stream_max < min_top:
                        stream_max = min_top
                        self.debug_print(f"Stream {stream_name}: no visible data, using full range with min_top {min_top:.1f}")

                if stream_cfg and stream_cfg.display_range_max_bottom is not None:
                    max_bottom = stream_cfg.display_range_max_bottom
                    if stream_min > max_bottom:
                        stream_min = max_bottom
                        self.debug_print(f"Stream {stream_name}: no visible data, using full range with max_bottom {max_bottom:.1f}")

            if stream_max - stream_min < 1e-10:
                stream_max = stream_min + 1.0

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
