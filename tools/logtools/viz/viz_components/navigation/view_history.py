"""
View history management for undo/redo navigation.

Maintains a stack of view states to support undo/redo operations.
"""

from PySide6.QtCore import QObject, Signal


class ViewHistory(QObject):
    """Manages undo/redo history for view navigation"""

    # Signal emitted when history state changes (for updating UI buttons)
    history_changed = Signal(bool, bool)  # can_undo, can_redo

    def __init__(self, max_history=50):
        """
        Initialize view history.

        Args:
            max_history: Maximum number of history entries to keep
        """
        super().__init__()
        self.history = []
        self.history_index = -1
        self.max_history = max_history

    def push(self, start, end, y_min=0, y_max=1):
        """
        Add a new view state to history.

        Args:
            start: View start time
            end: View end time
            y_min: View Y minimum (currently unused, always 0)
            y_max: View Y maximum (currently unused, always 1)
        """
        # Truncate forward history when adding a new state
        self.history = self.history[:self.history_index + 1]

        # Add new state (Y values stored for compatibility but not currently used)
        self.history.append((start, end, y_min, y_max))
        self.history_index += 1

        # Limit history size
        if len(self.history) > self.max_history:
            self.history.pop(0)
            self.history_index -= 1

        self._emit_state()

    def undo(self):
        """
        Undo to previous view state.

        The history works as follows:
        - history[history_index] is the CURRENT view being displayed
        - Undo should go to history[history_index - 1] (the previous view)
        - After undo, we're displaying history[history_index - 1], so index should point there

        Returns:
            Tuple of (start, end, y_min, y_max) or None if can't undo
        """
        # Can only undo if there's a previous state to go back to
        if self.history_index > 0:
            self.history_index -= 1
            self._emit_state()
            return self.history[self.history_index]
        return None

    def redo(self):
        """
        Redo to next view state.

        Returns:
            Tuple of (start, end, y_min, y_max) or None if can't redo
        """
        if self.history_index < len(self.history) - 1:
            self.history_index += 1
            self._emit_state()
            return self.history[self.history_index]
        return None

    def can_undo(self):
        """Check if undo is possible"""
        return self.history_index > 0

    def can_redo(self):
        """Check if redo is possible"""
        return self.history_index < len(self.history) - 1

    def clear(self):
        """Clear all history"""
        self.history = []
        self.history_index = -1
        self._emit_state()

    def _emit_state(self):
        """Emit signal with current history state"""
        self.history_changed.emit(self.can_undo(), self.can_redo())
