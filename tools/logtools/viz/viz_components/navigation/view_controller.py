"""
View navigation controller for zoom and pan operations.

Manages the current view window and provides methods for navigation.
"""

from PySide6.QtCore import QObject, Signal


class ViewNavigationController(QObject):
    """Manages view navigation (zoom, pan, fit)"""

    # Signal emitted when view changes
    view_changed = Signal(float, float, float, float)  # start, end, y_min, y_max

    def __init__(self, time_min=0.0, time_max=100.0):
        """
        Initialize view navigation controller.

        Args:
            time_min: Minimum time in dataset
            time_max: Maximum time in dataset
        """
        super().__init__()
        self.time_min = time_min
        self.time_max = time_max
        self.time_span = time_max - time_min

        # Current view window
        self.view_start = time_min
        self.view_end = time_max
        self.view_y_min = 0.0
        self.view_y_max = 1.0

        # Initial view for reset
        self.initial_view_start = time_min
        self.initial_view_end = time_max

    def set_time_bounds(self, time_min, time_max):
        """
        Update the time bounds from loaded data.

        Args:
            time_min: Minimum time in dataset
            time_max: Maximum time in dataset
        """
        self.time_min = time_min
        self.time_max = time_max
        self.time_span = time_max - time_min

    def set_view_range(self, start, end, y_min=0.0, y_max=1.0):
        """
        Set the current view range.

        Args:
            start: View start time
            end: View end time
            y_min: View Y minimum (currently unused)
            y_max: View Y maximum (currently unused)
        """
        self.view_start = start
        self.view_end = end
        self.view_y_min = y_min
        self.view_y_max = y_max
        self.view_changed.emit(start, end, y_min, y_max)

    def set_initial_view(self, start, end):
        """
        Set the initial view for reset operations.

        Args:
            start: Initial view start time
            end: Initial view end time
        """
        self.initial_view_start = start
        self.initial_view_end = end

    def get_view_range(self):
        """
        Get the current view range.

        Returns:
            Tuple of (start, end, y_min, y_max)
        """
        return (self.view_start, self.view_end, self.view_y_min, self.view_y_max)

    def zoom_in_2x(self):
        """Zoom in by 2x (halve the time shown), centered on current view"""
        current_duration = self.view_end - self.view_start
        center = (self.view_start + self.view_end) / 2

        # Halve the duration
        new_duration = current_duration / 2

        # Calculate new start/end centered on the same point
        new_start = center - new_duration / 2
        new_end = center + new_duration / 2

        # Clamp to valid range
        new_start = max(self.time_min, new_start)
        new_end = min(self.time_max, new_end)

        # If we hit a boundary, adjust the other side to maintain zoom if possible
        if new_start == self.time_min:
            new_end = min(self.time_min + new_duration, self.time_max)
        elif new_end == self.time_max:
            new_start = max(self.time_min, self.time_max - new_duration)

        self.set_view_range(new_start, new_end)

    def zoom_out_2x(self):
        """Zoom out by 2x (double the time shown), centered on current view"""
        current_duration = self.view_end - self.view_start
        center = (self.view_start + self.view_end) / 2

        # Double the duration
        new_duration = current_duration * 2

        # Calculate new start/end centered on the same point
        new_start = center - new_duration / 2
        new_end = center + new_duration / 2

        # Clamp to valid range
        new_start = max(self.time_min, new_start)
        new_end = min(self.time_max, new_end)

        # If we hit a boundary, adjust the other side to maintain 2x zoom if possible
        if new_start == self.time_min:
            new_end = min(self.time_min + new_duration, self.time_max)
        elif new_end == self.time_max:
            new_start = max(self.time_min, self.time_max - new_duration)

        self.set_view_range(new_start, new_end)

    def pan_left(self, percentage):
        """
        Pan left by a percentage of current view duration.

        Args:
            percentage: Percentage to pan (e.g., 0.5 for 50%)
        """
        current_duration = self.view_end - self.view_start
        shift = current_duration * percentage

        # Shift left (decrease both start and end)
        new_start = self.view_start - shift
        new_end = self.view_end - shift

        # Clamp to valid range
        if new_start < self.time_min:
            new_start = self.time_min
            new_end = self.time_min + current_duration

        self.set_view_range(new_start, new_end)

    def pan_right(self, percentage):
        """
        Pan right by a percentage of current view duration.

        Args:
            percentage: Percentage to pan (e.g., 0.5 for 50%)
        """
        current_duration = self.view_end - self.view_start
        shift = current_duration * percentage

        # Shift right (increase both start and end)
        new_start = self.view_start + shift
        new_end = self.view_end + shift

        # Clamp to valid range
        if new_end > self.time_max:
            new_end = self.time_max
            new_start = self.time_max - current_duration

        self.set_view_range(new_start, new_end)

    def reset_to_initial(self):
        """Reset view to initial state"""
        self.set_view_range(self.initial_view_start, self.initial_view_end)

    def zoom_to_region(self, x_min, x_max, y_min, y_max):
        """
        Zoom to a specific region (e.g., from rubber-band selection).

        Args:
            x_min: Region start time
            x_max: Region end time
            y_min: Region Y minimum (currently unused)
            y_max: Region Y maximum (currently unused)
        """
        # Clamp to valid range
        x_min = max(self.time_min, x_min)
        x_max = min(self.time_max, x_max)

        self.set_view_range(x_min, x_max, y_min, y_max)
