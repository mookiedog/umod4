"""
Data decimation utilities for performance optimization.

Provides algorithms for reducing the number of data points while preserving
key features (peaks, valleys, trends) in time-series data.
"""

import numpy as np


def min_max_decimate(time_array, value_array, target_points):
    """Decimate data while preserving min/max peaks in each bin.

    This ensures that when zoomed out, you still see all the peaks and valleys
    in the data, unlike simple decimation which might miss spikes.

    For each bin, we keep: first point, min, max, and last point to ensure
    continuity and preserve all features.

    Args:
        time_array: numpy array of time values
        value_array: numpy array of data values
        target_points: desired number of output points

    Returns:
        tuple of (decimated_time, decimated_values) numpy arrays
    """
    n = len(time_array)

    # If already small enough, return as-is
    if n <= target_points:
        return time_array, value_array

    # Each bin contributes up to 4 points (first, min, max, last)
    # So we need target_points/4 bins
    num_bins = max(1, target_points // 4)
    bin_size = max(1, n // num_bins)

    result_time = []
    result_values = []

    for i in range(0, n, bin_size):
        bin_time = time_array[i:i+bin_size]
        bin_values = value_array[i:i+bin_size]

        if len(bin_values) == 0:
            continue

        # Always keep first and last points of each bin for continuity
        first_idx = 0
        last_idx = len(bin_values) - 1

        # Find min and max indices within this bin
        min_idx = bin_values.argmin()
        max_idx = bin_values.argmax()

        # Collect unique indices in time order
        indices = sorted(set([first_idx, min_idx, max_idx, last_idx]))

        # Add all unique points in time order
        for idx in indices:
            result_time.append(bin_time[idx])
            result_values.append(bin_values[idx])

    return np.array(result_time), np.array(result_values)
