"""
Data Visualization Tool - Complete Implementation
Professional time-series data visualization with advanced features

Requirements:
pip install PyQt6 pyqtgraph numpy pandas h5py

Usage:
python viz.py [logfile.h5]
"""

import sys
import os
import argparse
import numpy as np
import pandas as pd
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                             QHBoxLayout, QPushButton, QFileDialog, QCheckBox,
                             QScrollArea, QSplitter, QLabel, QFrame, QMenu,
                             QMenuBar, QToolBar, QLineEdit, QListWidget, QListWidgetItem,
                             QSpinBox)
from PyQt6.QtCore import Qt, QPointF, QTimer, QRectF, QSettings, QByteArray, QMimeData, QPoint
from PyQt6.QtGui import QPen, QColor, QFont, QAction, QPainter, QDrag
import pyqtgraph as pg
from pyqtgraph import QtWidgets

try:
    import h5py
    HDF5_AVAILABLE = True
except ImportError:
    HDF5_AVAILABLE = False
    h5py = None

# Event visualization constants
SPARK_LABEL_OFFSET = 0.05  # Vertical offset from RPM line for spark labels (5% of normalized range)
CRANKREF_LINE_HEIGHT = 0.08  # Height of vertical line for crankref markers (8% of normalized range)
CAMSHAFT_LINE_HEIGHT = 0.08  # Height of vertical line for camshaft markers (8% of normalized range)


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

class ResizableSplitter(QSplitter):
    """Custom splitter that can hide/show panes with minimum size handling"""
    def __init__(self, orientation, parent=None):
        super().__init__(orientation, parent)
        self.setHandleWidth(3)
        self.setStyleSheet("QSplitter::handle { background-color: black; }")
        self.min_size = 40
        
class ColorCheckbox(QCheckBox):
    """Custom checkbox that fills with color when checked instead of showing a checkmark"""
    def __init__(self, color, parent=None):
        super().__init__(parent)
        self.fill_color = color
        self.setFixedSize(16, 16)
        # Remove default styling
        self.setStyleSheet("""
            QCheckBox::indicator {
                width: 16px;
                height: 16px;
                border: 2px solid #888;
                border-radius: 3px;
                background: white;
            }
            QCheckBox::indicator:checked {
                background: white;
            }
        """)

    def paintEvent(self, event):
        """Custom paint to fill with color when checked"""
        super().paintEvent(event)
        if self.isChecked():
            from PyQt6.QtGui import QPainter, QColor, QPen
            painter = QPainter(self)
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)

            # Fill the checkbox with the stream color
            painter.fillRect(2, 2, 12, 12, QColor(self.fill_color))
            painter.end()


class StreamCheckbox(QWidget):
    """Custom widget for stream selection with color-filled checkbox and colored label - supports drag and drop"""
    def __init__(self, stream_name, color, parent=None):
        super().__init__(parent)
        self.stream_name = stream_name
        self.color = color
        self.drag_start_pos = None
        self.dark_theme = False  # Track current theme
        self.display_mode = "line"  # "line" or "points"
        self.color_change_callback = None  # Callback for color changes
        self.display_mode_callback = None  # Callback for display mode changes

        layout = QHBoxLayout()
        layout.setContentsMargins(2, 2, 2, 2)
        layout.setSpacing(5)

        # Use custom color-filled checkbox
        self.checkbox = ColorCheckbox(color)
        self.checkbox.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.checkbox.customContextMenuRequested.connect(self.show_color_picker)

        # Use monospace font for stream names
        mono_font = QFont("Monospace", 9)
        mono_font.setStyleHint(QFont.StyleHint.TypeWriter)

        # Create a drag handle area (the label area)
        self.label = QLabel(stream_name)
        self.label.setFont(mono_font)
        self.label.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.label.customContextMenuRequested.connect(self.show_stream_context_menu)
        self.update_label_style()  # Set initial style

        # Install event filter on label to capture mouse events
        self.label.installEventFilter(self)

        layout.addWidget(self.checkbox)
        layout.addWidget(self.label, stretch=1)

        self.setLayout(layout)

    def update_label_style(self):
        """Update label text color based on theme - use basic readable colors"""
        # Use simple readable colors based on theme, not stream color
        if self.dark_theme:
            text_color = "#e0e0e0"  # Light gray for dark theme
        else:
            text_color = "#202020"  # Dark gray for light theme

        self.label.setStyleSheet(f"color: {text_color};")

    def set_theme(self, dark_theme):
        """Update theme for this widget"""
        self.dark_theme = dark_theme
        self.update_label_style()

    def set_font_size(self, size):
        """Update font size for the label"""
        mono_font = QFont("Monospace", size)
        mono_font.setStyleHint(QFont.StyleHint.TypeWriter)
        self.label.setFont(mono_font)

    def show_color_picker(self, pos):
        """Show color picker dialog when right-clicking checkbox"""
        from PyQt6.QtWidgets import QColorDialog
        from PyQt6.QtGui import QColor

        initial_color = QColor(self.color)
        color = QColorDialog.getColor(initial_color, self, f"Choose color for {self.stream_name}")

        if color.isValid():
            self.color = color.name()
            self.checkbox.fill_color = self.color
            self.checkbox.update()  # Redraw checkbox

            # Notify parent via callback
            if self.color_change_callback:
                self.color_change_callback(self.stream_name, self.color)

    def show_stream_context_menu(self, pos):
        """Show context menu when right-clicking stream name"""
        from PyQt6.QtWidgets import QMenu
        from PyQt6.QtGui import QAction

        menu = QMenu(self)

        # Display mode toggle
        if self.display_mode == "line":
            action_text = "Display as Points"
        else:
            action_text = "Display as Line"

        toggle_action = QAction(action_text, self)
        toggle_action.triggered.connect(self.toggle_display_mode)
        menu.addAction(toggle_action)

        menu.exec(self.label.mapToGlobal(pos))

    def toggle_display_mode(self):
        """Toggle between line and points display mode"""
        if self.display_mode == "line":
            self.display_mode = "points"
        else:
            self.display_mode = "line"

        # Notify parent via callback
        if self.display_mode_callback:
            self.display_mode_callback(self.stream_name, self.display_mode)

    def eventFilter(self, obj, event):
        """Filter events on the label to handle dragging"""
        if obj == self.label:
            if event.type() == event.Type.MouseButtonPress:
                if event.button() == Qt.MouseButton.LeftButton:
                    self.drag_start_pos = event.pos()
                    return False
            elif event.type() == event.Type.MouseMove:
                if event.buttons() & Qt.MouseButton.LeftButton:
                    if self.drag_start_pos is not None:
                        distance = (event.pos() - self.drag_start_pos).manhattanLength()
                        if distance >= QApplication.startDragDistance():
                            self.start_drag()
                            return True
        return super().eventFilter(obj, event)

    def start_drag(self):
        """Initiate the drag operation"""
        drag = QDrag(self)
        mime_data = QMimeData()
        mime_data.setText(self.stream_name)
        drag.setMimeData(mime_data)

        # Execute drag
        result = drag.exec(Qt.DropAction.MoveAction)
        self.drag_start_pos = None


class DraggableStreamList(QWidget):
    """Custom list widget that supports drag-and-drop reordering with visual feedback"""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAcceptDrops(True)

        self.layout = QVBoxLayout()
        self.layout.setContentsMargins(0, 0, 0, 0)
        self.layout.setSpacing(2)
        self.layout.addStretch()
        self.setLayout(self.layout)

        self.stream_widgets = []
        self.drop_indicator_pos = -1  # -1 means no indicator
        self.dragging = False
        self.reorder_callback = None  # Callback to notify parent of reorder

    def add_stream_widget(self, widget):
        """Add a stream widget to the list"""
        # Insert before the stretch
        insert_pos = self.layout.count() - 1
        self.layout.insertWidget(insert_pos, widget)
        self.stream_widgets.append(widget)

    def clear_streams(self):
        """Clear all stream widgets"""
        while self.layout.count() > 1:  # Keep the stretch
            item = self.layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        self.stream_widgets.clear()

    def get_stream_order(self):
        """Get the current order of stream names"""
        order = []
        for i in range(self.layout.count() - 1):  # Exclude stretch
            widget = self.layout.itemAt(i).widget()
            if isinstance(widget, StreamCheckbox):
                order.append(widget.stream_name)
        return order

    def reorder_to_match(self, stream_order):
        """Reorder widgets to match the given order"""
        # Create a mapping of stream name to widget
        widget_map = {}
        for widget in self.stream_widgets:
            widget_map[widget.stream_name] = widget

        # Remove all widgets from layout (except stretch)
        while self.layout.count() > 1:
            self.layout.takeAt(0)

        # Re-add in the specified order
        for stream_name in stream_order:
            if stream_name in widget_map:
                self.layout.insertWidget(self.layout.count() - 1, widget_map[stream_name])

    def dragEnterEvent(self, event):
        """Accept drag events"""
        if event.mimeData().hasText():
            event.acceptProposedAction()
            self.dragging = True

    def dragMoveEvent(self, event):
        """Update drop indicator position during drag"""
        if event.mimeData().hasText():
            # Find insertion position based on mouse Y coordinate
            y_pos = event.position().y()
            insert_index = self._get_drop_index(y_pos)

            if insert_index != self.drop_indicator_pos:
                self.drop_indicator_pos = insert_index
                self.update()  # Trigger repaint

            event.acceptProposedAction()

    def dragLeaveEvent(self, event):
        """Clear drop indicator when drag leaves"""
        self.drop_indicator_pos = -1
        self.dragging = False
        self.update()

    def dropEvent(self, event):
        """Handle drop - reorder the widgets"""
        if event.mimeData().hasText():
            stream_name = event.mimeData().text()
            insert_index = self._get_drop_index(event.position().y())

            # Find the widget being dragged
            dragged_widget = None
            old_index = -1
            for i, widget in enumerate(self.stream_widgets):
                if widget.stream_name == stream_name:
                    dragged_widget = widget
                    old_index = i
                    break

            if dragged_widget and insert_index != old_index:
                # Remove from old position
                self.layout.removeWidget(dragged_widget)

                # Adjust insert index if moving down
                if insert_index > old_index:
                    insert_index -= 1

                # Insert at new position
                self.layout.insertWidget(insert_index, dragged_widget)

                # Update internal list
                self.stream_widgets.remove(dragged_widget)
                self.stream_widgets.insert(insert_index, dragged_widget)

                # Notify parent about reorder via callback
                if self.reorder_callback:
                    self.reorder_callback()

            event.acceptProposedAction()

        self.drop_indicator_pos = -1
        self.dragging = False
        self.update()

    def _get_drop_index(self, y_pos):
        """Calculate the insertion index based on Y position"""
        for i in range(self.layout.count() - 1):  # Exclude stretch
            widget = self.layout.itemAt(i).widget()
            if widget:
                widget_y = widget.y()
                widget_height = widget.height()

                # If above midpoint, insert before this widget
                if y_pos < widget_y + widget_height / 2:
                    return i

        # Insert at end
        return self.layout.count() - 1

    def paintEvent(self, event):
        """Draw drop indicator line"""
        super().paintEvent(event)

        if self.drop_indicator_pos >= 0 and self.dragging:
            painter = QPainter(self)
            painter.setPen(QPen(QColor(0, 120, 215), 2))  # Blue line

            # Calculate Y position for the indicator
            if self.drop_indicator_pos < self.layout.count() - 1:
                widget = self.layout.itemAt(self.drop_indicator_pos).widget()
                if widget:
                    y = widget.y() - 1
                else:
                    y = 0
            else:
                # Draw at bottom
                if self.layout.count() > 1:
                    last_widget = self.layout.itemAt(self.layout.count() - 2).widget()
                    if last_widget:
                        y = last_widget.y() + last_widget.height()
                    else:
                        y = 0
                else:
                    y = 0

            # Draw horizontal line
            painter.drawLine(0, y, self.width(), y)
            painter.end()
        
class ZoomableGraphWidget(pg.PlotWidget):
    """Graph widget with rubber band zoom capability"""
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.rubberband_start = None
        self.rubberband_rect = None
        self.is_dragging = False
        self.zoom_callback = None
        
    def mousePressEvent(self, ev):
        if ev.button() == Qt.MouseButton.LeftButton:
            pos = QPointF(ev.pos())
            self.rubberband_start = self.plotItem.vb.mapSceneToView(pos)
            self.is_dragging = True
            ev.accept()
        else:
            super().mousePressEvent(ev)
            
    def mouseMoveEvent(self, ev):
        if self.is_dragging and self.rubberband_start is not None:
            if self.rubberband_rect is not None:
                self.plotItem.vb.removeItem(self.rubberband_rect)
            
            pos = QPointF(ev.pos())
            current = self.plotItem.vb.mapSceneToView(pos)
            
            x = min(self.rubberband_start.x(), current.x())
            y = min(self.rubberband_start.y(), current.y())
            w = abs(current.x() - self.rubberband_start.x())
            h = abs(current.y() - self.rubberband_start.y())
            
            self.rubberband_rect = QtWidgets.QGraphicsRectItem(x, y, w, h)
            self.rubberband_rect.setPen(pg.mkPen('b', width=2, style=Qt.PenStyle.DashLine))
            self.rubberband_rect.setBrush(pg.mkBrush(100, 150, 255, 50))
            self.plotItem.vb.addItem(self.rubberband_rect)
            ev.accept()
        else:
            super().mouseMoveEvent(ev)
            
    def mouseReleaseEvent(self, ev):
        if ev.button() == Qt.MouseButton.LeftButton and self.is_dragging:
            if self.rubberband_start is not None:
                pos = QPointF(ev.pos())
                end = self.plotItem.vb.mapSceneToView(pos)
                
                x_min = min(self.rubberband_start.x(), end.x())
                x_max = max(self.rubberband_start.x(), end.x())
                y_min = min(self.rubberband_start.y(), end.y())
                y_max = max(self.rubberband_start.y(), end.y())
                
                if abs(x_max - x_min) > 0.01 and abs(y_max - y_min) > 0.01:
                    if self.zoom_callback:
                        self.zoom_callback(x_min, x_max, y_min, y_max)
            
            if self.rubberband_rect is not None:
                self.plotItem.vb.removeItem(self.rubberband_rect)
                self.rubberband_rect = None
            self.rubberband_start = None
            self.is_dragging = False
            ev.accept()
        else:
            super().mouseReleaseEvent(ev)

# ================================================================================================
# Data Decimation Functions
# ================================================================================================

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

class DataVisualizationTool(QMainWindow):
    def __init__(self):
        super().__init__()

        # Initialize configuration manager FIRST
        self.config = AppConfig()

        # Print config location for user awareness
        print(f"Configuration file: {self.config.settings.fileName()}")

        self.base_title = "Data Visualization Tool - v1.1"
        self.setWindowTitle(self.base_title)
        self.setGeometry(100, 100, 1600, 900)

        # Data storage
        self.df = None
        self.raw_data = {}  # Store raw unsampled data from HDF5
        self.data_streams = []
        self.stream_colors = {}
        self.stream_metadata = {}  # Store unit information per stream
        self.stream_ranges = {}  # Store min/max range for each stream
        self.enabled_streams = []
        self.axis_owner = None  # Stream that owns the left Y-axis
        self.right_axis_owner = None  # Stream that owns the right Y-axis
        self.current_file = None  # Track currently loaded file

        # View state
        self.view_start = 0
        self.view_end = self.config.get("default_view_duration")
        self.view_y_min = 0
        self.view_y_max = 100
        self.total_time_span = 0
        self.initial_view_start = 0
        self.initial_view_end = self.config.get("default_view_duration")

        # History for undo/redo
        self.history = []
        self.history_index = -1

        # Update timer for real-time updates (30 FPS max)
        self.update_timer = QTimer()
        self.update_timer.setInterval(33)  # ~30 FPS
        self.update_timer.timeout.connect(self.perform_pending_update)
        self.pending_update = False

        # Color palette
        self.colors = [
            '#e41a1c', '#377eb8', '#4daf4a', '#984ea3', '#ff7f00',
            '#a65628', '#f781bf', '#999999', '#66c2a5', '#fc8d62',
            '#8da0cb', '#e78ac3', '#a6d854', '#ffd92f', '#e5c494',
            '#b3b3b3', '#8dd3c7', '#ffffb3', '#bebada', '#1f78b4',
            '#33a02c', '#fb9a99', '#e31a1c', '#fdbf6f', '#cab2d6',
            '#6a3d9a', '#ffff99', '#b15928', '#d9d9d9', '#bc80bd'
        ]

        # Theme state (load from config)
        self.dark_theme = (self.config.get("theme") == "dark")

        # Font size configuration - default is 12pt
        self.axis_font_size = self.config.get("axis_font_size", 12)

        self.init_ui()

        # Restore window geometry after UI initialization
        self.config.restore_window_geometry(self)
        
    def init_ui(self):
        # Create main widget with grey border
        central_widget = QWidget()
        central_widget.setStyleSheet("QWidget { border: 2px solid grey; }")
        self.setCentralWidget(central_widget)
        
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(2, 2, 2, 2)
        main_layout.setSpacing(0)

        # Create menu bar
        self.create_menu_bar()

        # Apply initial window border based on theme
        self.apply_window_border()

        # Create horizontal splitter for main content
        self.h_splitter = ResizableSplitter(Qt.Orientation.Horizontal)

        # Stream selection window
        self.create_stream_selection()
        self.h_splitter.addWidget(self.stream_selection)

        # Create vertical splitter for graph and navigation
        self.v_splitter = ResizableSplitter(Qt.Orientation.Vertical)

        # Graph window
        self.create_graph_window()
        self.v_splitter.addWidget(self.graph_plot)

        # Navigation window
        self.create_navigation_window()
        self.v_splitter.addWidget(self.nav_widget)

        # Set default sizes
        self.v_splitter.setSizes([800, 50])
        self.v_splitter.splitterMoved.connect(self.on_splitter_moved)

        self.h_splitter.addWidget(self.v_splitter)
        self.h_splitter.setSizes([150, 1450])

        main_layout.addWidget(self.h_splitter)

        # Restore splitter states after creation
        self.config.restore_splitter_state("horizontal", self.h_splitter)
        self.config.restore_splitter_state("vertical", self.v_splitter)
        
    def create_menu_bar(self):
        """Create the menu bar to replace the ribbon"""
        menu_bar = self.menuBar()

        # FILE MENU
        file_menu = menu_bar.addMenu("&File")

        # Load action
        load_action = QAction("&Load HDF5 Log File...", self)
        load_action.setShortcut("Ctrl+O")
        load_action.triggered.connect(self.load_hdf5_file)
        load_action.setEnabled(HDF5_AVAILABLE)
        if not HDF5_AVAILABLE:
            load_action.setText("&Load HDF5 Log File... (h5py not installed)")
        file_menu.addAction(load_action)

        # Recent files submenu
        self.recent_files_menu = file_menu.addMenu("Recent Files")
        self.update_recent_files_menu()

        file_menu.addSeparator()

        # Exit action
        exit_action = QAction("E&xit", self)
        exit_action.setShortcut("Ctrl+Q")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        # VIEW MENU
        view_menu = menu_bar.addMenu("&View")

        # Undo action
        self.undo_action = QAction("&Undo", self)
        self.undo_action.setShortcut("Ctrl+Z")
        self.undo_action.triggered.connect(self.undo)
        self.undo_action.setEnabled(False)
        view_menu.addAction(self.undo_action)

        # Redo action
        self.redo_action = QAction("&Redo", self)
        self.redo_action.setShortcut("Ctrl+Y")
        self.redo_action.triggered.connect(self.redo)
        self.redo_action.setEnabled(False)
        view_menu.addAction(self.redo_action)

        view_menu.addSeparator()

        # Reset View
        reset_action = QAction("&Reset View", self)
        reset_action.setShortcut("Ctrl+R")
        reset_action.triggered.connect(self.reset_view)
        view_menu.addAction(reset_action)

        # Fit
        fit_action = QAction("&Fit", self)
        fit_action.setShortcut("Ctrl+F")
        fit_action.triggered.connect(self.fit_axis_owner)
        view_menu.addAction(fit_action)

        # Fit All
        fit_all_action = QAction("Fit &All", self)
        fit_all_action.setShortcut("Ctrl+Shift+F")
        fit_all_action.triggered.connect(self.fit_all)
        view_menu.addAction(fit_all_action)

        view_menu.addSeparator()

        # Zoom Out 2x
        zoom_out_action = QAction("Zoom &Out 2x", self)
        zoom_out_action.setShortcut("Ctrl+O")
        zoom_out_action.triggered.connect(self.zoom_out_2x)
        view_menu.addAction(zoom_out_action)

        # Pan Left 50%
        pan_left_action = QAction("Pan &Left 50%", self)
        pan_left_action.setShortcut("Ctrl+Left")
        pan_left_action.triggered.connect(self.pan_left_50)
        view_menu.addAction(pan_left_action)

        # Pan Right 50%
        pan_right_action = QAction("Pan Right 50%", self)
        pan_right_action.setShortcut("Ctrl+Right")
        pan_right_action.triggered.connect(self.pan_right_50)
        view_menu.addAction(pan_right_action)

        view_menu.addSeparator()

        # Show Ride on Maps
        show_map_action = QAction("Show Ride on &Maps", self)
        show_map_action.setShortcut("Ctrl+M")
        show_map_action.triggered.connect(self.show_ride_on_maps)
        view_menu.addAction(show_map_action)

        # Metadata viewer
        metadata_action = QAction("View HDF5 Meta&data", self)
        metadata_action.triggered.connect(self.show_metadata_dialog)
        metadata_action.setEnabled(HDF5_AVAILABLE)
        view_menu.addAction(metadata_action)

        # SETTINGS MENU
        settings_menu = menu_bar.addMenu("&Settings")

        # Theme submenu
        theme_menu = settings_menu.addMenu("&Theme")

        # Light theme action
        self.light_theme_action = QAction("&Light", self)
        self.light_theme_action.setCheckable(True)
        self.light_theme_action.triggered.connect(lambda: self.set_theme(False))
        theme_menu.addAction(self.light_theme_action)

        # Dark theme action
        self.dark_theme_action = QAction("&Dark", self)
        self.dark_theme_action.setCheckable(True)
        self.dark_theme_action.triggered.connect(lambda: self.set_theme(True))
        theme_menu.addAction(self.dark_theme_action)

        # Set initial theme checkmark
        if self.dark_theme:
            self.dark_theme_action.setChecked(True)
        else:
            self.light_theme_action.setChecked(True)

        # Font size submenu
        font_menu = settings_menu.addMenu("&Font Size")

        font_sizes = [8, 10, 12, 14, 16, 18, 20, 24]
        self.font_size_actions = {}  # Store references to update checkmarks
        for size in font_sizes:
            font_action = QAction(f"{size} pt", self)
            font_action.setCheckable(True)
            font_action.triggered.connect(lambda checked, s=size: self.change_font_size(s))
            font_menu.addAction(font_action)
            self.font_size_actions[size] = font_action
            # Set initial checkmark
            if size == self.axis_font_size:
                font_action.setChecked(True)

    def change_font_size(self, size):
        """Change the axis font size and stream label font size"""
        self.axis_font_size = size
        self.config.set("axis_font_size", size)

        # Update checkmarks in font size menu
        for font_size, action in self.font_size_actions.items():
            action.setChecked(font_size == size)

        # Update all stream widgets with new font size
        for widget in self.stream_list_widget.stream_widgets:
            widget.set_font_size(size)

        self.update_graph_plot()

    def create_stream_selection(self):
        """Create the stream selection window"""
        self.stream_selection = QWidget()
        self.stream_selection.setMinimumWidth(40)

        layout = QVBoxLayout(self.stream_selection)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # Add search/filter box
        self.stream_filter = QLineEdit()
        self.stream_filter.setPlaceholderText("Filter streams...")
        self.stream_filter.textChanged.connect(self.filter_streams)
        layout.addWidget(self.stream_filter)

        # Scroll area
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)

        # Use custom draggable list widget
        self.stream_list_widget = DraggableStreamList()
        # Set up the reorder callback
        self.stream_list_widget.reorder_callback = self.on_stream_reorder

        scroll.setWidget(self.stream_list_widget)
        layout.addWidget(scroll)
        
    def create_graph_window(self):
        """Create the main graph window"""
        self.graph_plot = ZoomableGraphWidget()
        self.graph_plot.setBackground('w')
        self.graph_plot.showGrid(x=True, y=True, alpha=0.3)
        self.graph_plot.setLabel('bottom', 'Time (s)')
        self.graph_plot.setLabel('left', 'Value')
        self.graph_plot.getPlotItem().setMouseEnabled(x=False, y=False)
        self.graph_plot.zoom_callback = self.handle_graph_zoom
        self.graph_plot.setYRange(0, 100)

        # Enable and show the right Y-axis
        self.graph_plot.showAxis('right')
        self.graph_plot.setLabel('right', 'Value')

        # Ensure font sizes are set properly for initial state
        label_font = QFont()
        label_font.setPointSize(self.axis_font_size)
        self.graph_plot.getAxis('bottom').setTickFont(label_font)
        self.graph_plot.getAxis('left').setTickFont(label_font)
        self.graph_plot.getAxis('right').setTickFont(label_font)

        # Create GPS position marker scatter plot (triangles above time axis)
        # These will be persistent and updated when data loads or view changes
        self.gps_markers = pg.ScatterPlotItem(
            symbol='t',  # 't' = triangle pointing up
            size=12,
            pen=pg.mkPen(color='blue', width=2),
            brush=pg.mkBrush(color='blue'),
            hoverable=True,  # Enable hover detection
            hoverPen=pg.mkPen(color='red', width=3),  # Red when hovering
            hoverBrush=pg.mkBrush(pg.mkColor(255, 100, 100)),  # Light red fill when hovering
            tip=None  # No tooltip for now
        )

        # Connect to sigClicked signal
        # Note: This requires double-click by design in pyqtgraph
        self.gps_markers.sigClicked.connect(self.on_gps_marker_clicked)

        self.graph_plot.addItem(self.gps_markers)
        
    def create_navigation_window(self):
        """Create the navigation window"""
        self.nav_widget = QWidget()
        self.nav_widget.setMinimumHeight(40)
        self.nav_widget.setMaximumHeight(200)
        
        nav_layout = QVBoxLayout(self.nav_widget)
        nav_layout.setContentsMargins(0, 0, 0, 0)
        
        self.nav_plot = pg.PlotWidget()
        self.nav_plot.setBackground('w')
        self.nav_plot.setLabel('bottom', 'Time (s)')
        self.nav_plot.hideAxis('left')
        self.nav_plot.setMouseEnabled(x=False, y=False)
        
        # Add linear region for view selection
        self.view_region = pg.LinearRegionItem(
            [0, 10],
            brush=pg.mkBrush(100, 150, 255, 100),
            pen=pg.mkPen('b', width=2),
            movable=True
        )
        self.view_region.setZValue(10)
        self.nav_plot.addItem(self.view_region)
        self.view_region.sigRegionChanged.connect(self.on_region_changed)
        self.view_region.sigRegionChangeFinished.connect(self.on_region_change_finished)
        
        nav_layout.addWidget(self.nav_plot)
        
    def populate_stream_selection(self):
        """Populate the stream selection window"""
        # Clear existing
        self.stream_list_widget.clear_streams()
        self.stream_colors.clear()

        # Get saved stream order, or use default order
        saved_order = self.config.get_stream_order()

        # Determine display order
        if saved_order:
            # Filter to only include streams that exist in current file
            display_order = [s for s in saved_order if s in self.data_streams]
            # Add any new streams not in saved order
            for stream in self.data_streams:
                if stream not in display_order:
                    display_order.append(stream)
        else:
            display_order = self.data_streams[:]

        # Create checkboxes for each stream in display order
        for i, stream in enumerate(display_order):
            color = self.colors[i % len(self.colors)]
            self.stream_colors[stream] = color

            stream_widget = StreamCheckbox(stream, color)
            stream_widget.set_theme(self.dark_theme)  # Initialize theme
            stream_widget.set_font_size(self.axis_font_size)  # Initialize font size

            # Set up callbacks
            stream_widget.color_change_callback = self.on_stream_color_changed
            stream_widget.display_mode_callback = self.on_stream_display_mode_changed

            # Load saved preferences
            saved_color = self.config.get(f"stream_color_{stream}")
            if saved_color:
                stream_widget.color = saved_color
                stream_widget.checkbox.fill_color = saved_color
                self.stream_colors[stream] = saved_color

            saved_mode = self.config.get(f"stream_display_mode_{stream}", "line")
            stream_widget.display_mode = saved_mode

            # Connect signal before setting default state so it fires
            stream_widget.checkbox.stateChanged.connect(
                lambda state, s=stream: self.toggle_stream(s, state)
            )

            # Enable ecu_rpm_instantaneous by default (signal will fire)
            if stream == "ecu_rpm_instantaneous":
                stream_widget.checkbox.setChecked(True)

            stream_widget.label.mousePressEvent = lambda ev, s=stream: self.on_stream_name_clicked(s, ev)

            self.stream_list_widget.add_stream_widget(stream_widget)

    def filter_streams(self, search_text):
        """Filter visible streams based on search text"""
        search_text = search_text.lower()
        for widget in self.stream_list_widget.stream_widgets:
            visible = search_text in widget.stream_name.lower()
            widget.setVisible(visible)

    def on_stream_reorder(self):
        """Called when streams are reordered via drag-and-drop"""
        # Get new order and save it
        new_order = self.stream_list_widget.get_stream_order()
        self.config.save_stream_order(new_order)

    def on_stream_color_changed(self, stream_name, new_color):
        """Called when a stream's color is changed via color picker"""
        self.stream_colors[stream_name] = new_color
        self.config.set(f"stream_color_{stream_name}", new_color)
        # Redraw plots with new color
        self.update_graph_plot()
        self.update_navigation_plot()

    def on_stream_display_mode_changed(self, stream_name, display_mode):
        """Called when a stream's display mode is changed (line/points)"""
        self.config.set(f"stream_display_mode_{stream_name}", display_mode)
        # Redraw plots with new display mode
        self.update_graph_plot()
        self.update_navigation_plot()

    def update_recent_files_menu(self):
        """Update the recent files menu."""
        self.recent_files_menu.clear()
        recent_files = self.config.get_recent_files()

        if not recent_files:
            no_files_action = QAction("No recent files", self)
            no_files_action.setEnabled(False)
            self.recent_files_menu.addAction(no_files_action)
        else:
            for filepath in recent_files:
                # Show just the filename, but use full path internally
                action = QAction(os.path.basename(filepath), self)
                action.setToolTip(filepath)  # Show full path in tooltip
                action.triggered.connect(lambda checked, f=filepath: self.load_recent_file(f))
                self.recent_files_menu.addAction(action)

            # Add separator and clear action
            self.recent_files_menu.addSeparator()
            clear_action = QAction("Clear Recent Files", self)
            clear_action.triggered.connect(self.clear_recent_files)
            self.recent_files_menu.addAction(clear_action)

    def load_recent_file(self, filepath):
        """Load a file from the recent files list."""
        if os.path.exists(filepath):
            self.load_hdf5_file_internal(filepath)
        else:
            print(f"File not found: {filepath}")
            # Remove from recent files
            recent = self.config.get_recent_files()
            if filepath in recent:
                recent.remove(filepath)
                self.config.settings.setValue("recent_files", recent)
                self.update_recent_files_menu()

    def clear_recent_files(self):
        """Clear the recent files list."""
        self.config.clear_recent_files()
        self.update_recent_files_menu()

    def load_hdf5_file(self):
        """Load data from an HDF5 file with file picker."""
        if not HDF5_AVAILABLE:
            print("Error: h5py is not installed. Cannot load HDF5 files.")
            return

        # Use last directory from config
        start_dir = self.config.get("default_file_location")

        filename, _ = QFileDialog.getOpenFileName(
            self,
            "Open HDF5 Log File",
            start_dir,
            "HDF5 Files (*.h5 *.hdf5);;All Files (*)"
        )

        if filename:
            self.load_hdf5_file_internal(filename)

    def load_hdf5_file_internal(self, filename):
        """Internal method to load HDF5 file (used by both file picker and recent files)."""
        if not os.path.exists(filename):
            print(f"Error: File not found: {filename}")
            return

        try:
            self.current_file = filename
            print(f"Loading HDF5 file: {filename}")

            with h5py.File(filename, 'r') as h5file:
                # Read metadata
                print("Metadata:")
                for key, value in h5file.attrs.items():
                    print(f"  {key}: {value}")

                # Collect all datasets
                raw_data = {}
                stream_names = []
                self.stream_metadata = {}  # Reset metadata

                # Get all dataset names
                for key in h5file.keys():
                    if key == 'eprom_loads':  # Skip special datasets
                        continue

                    ds = h5file[key]

                    # Handle different dataset shapes
                    if len(ds.shape) == 2 and ds.shape[1] == 2:
                        # Standard (time_ns, value) format
                        if ds.shape[0] > 0:  # Only include non-empty datasets
                            print(f"  Loading {key}: {ds.shape[0]} samples")

                            # Detect native units from dataset name
                            data_type, native_units = UnitConverter.parse_units_from_name(key)

                            # Get user's preferred display units
                            display_units = native_units  # Default to native
                            if data_type == 'temperature':
                                display_units = self.config.get_temperature_units()
                            elif data_type == 'velocity':
                                display_units = self.config.get_velocity_units()
                            elif data_type == 'pressure':
                                display_units = self.config.get_pressure_units()

                            # Store metadata for this stream
                            self.stream_metadata[key] = {
                                'data_type': data_type,
                                'native_units': native_units,
                                'display_units': display_units
                            }

                            # Load and convert data
                            time_data = ds[:, 0] / 1e9  # Convert ns to seconds
                            value_data = ds[:, 1]

                            # Apply unit conversion if needed
                            if data_type == 'temperature' and native_units != display_units:
                                value_data = UnitConverter.convert_temperature(value_data, native_units, display_units)
                                print(f"    Converted from {native_units} to {display_units}")
                            elif data_type == 'velocity' and native_units != display_units:
                                value_data = UnitConverter.convert_velocity(value_data, native_units, display_units)
                                print(f"    Converted from {native_units} to {display_units}")
                            elif data_type == 'pressure' and native_units != display_units:
                                value_data = UnitConverter.convert_pressure(value_data, native_units, display_units)
                                print(f"    Converted from {native_units} to {display_units}")

                            # Store converted data
                            raw_data[key] = {
                                'time': time_data,
                                'values': value_data
                            }
                            stream_names.append(key)

                    elif len(ds.shape) == 2 and ds.shape[1] == 3:
                        # 3D data like gps_position (time_ns, lat, lon)
                        if ds.shape[0] > 0:
                            print(f"  Loading {key}: {ds.shape[0]} samples (3D)")
                            time_ns = ds[:, 0] / 1e9
                            # Keep GPS position as a single entity (lat/lon cannot be separated)
                            if key == 'gps_position':
                                raw_data['gps_position'] = {
                                    'time': time_ns,
                                    'lat': ds[:, 1],
                                    'lon': ds[:, 2]
                                }
                                # Note: gps_position is NOT added to stream_names
                                # It will be displayed as markers, not as a plottable stream

                    elif len(ds.shape) == 1:
                        # 1D timestamp arrays (markers) - skip for now
                        if ds.shape[0] > 0:
                            print(f"  Skipping 1D marker dataset {key}: {ds.shape[0]} samples")

                print(f"\nTotal streams loaded: {len(stream_names)}")

                # Store raw data without resampling
                self.raw_data = raw_data
                self.data_streams = stream_names

                # Calculate and store the range for each stream (for normalization)
                # Also find the overall time bounds
                self.stream_ranges = {}
                all_times = []
                for stream in stream_names:
                    stream_min = float(raw_data[stream]['values'].min())
                    stream_max = float(raw_data[stream]['values'].max())
                    # Add a small epsilon to avoid division by zero for constant streams
                    if stream_max - stream_min < 1e-10:
                        stream_max = stream_min + 1.0
                    self.stream_ranges[stream] = (stream_min, stream_max)
                    print(f"  {stream}: range [{stream_min:.2f}, {stream_max:.2f}]")

                    # Collect all time values to find overall bounds
                    all_times.extend([raw_data[stream]['time'].min(), raw_data[stream]['time'].max()])

                # Set time bounds from raw data
                self.total_time_span = float(max(all_times))
                self.view_start = 0
                self.view_end = min(self.config.get("default_view_duration"), self.total_time_span * 0.1)
                self.initial_view_start = self.view_start
                self.initial_view_end = self.view_end

                print(f"Time span: {self.total_time_span:.2f} seconds")

                # Reset state
                self.enabled_streams = []
                self.axis_owner = None
                self.history = []
                self.history_index = -1
                self.update_history_buttons()

                self.populate_stream_selection()
                self.update_navigation_plot()
                self.update_graph_plot()

                # Add to recent files and update menu
                self.config.add_recent_file(filename)
                self.update_recent_files_menu()

                # Save the directory for next time
                self.config.set("default_file_location", os.path.dirname(filename))

                # Update window title with filename
                self.setWindowTitle(f"{self.base_title} - {os.path.basename(filename)}")

                print("HDF5 file loaded successfully!")

        except Exception as e:
            print(f"Error loading HDF5 file: {e}")
            import traceback
            traceback.print_exc()

    def show_metadata_dialog(self):
        """Show HDF5 metadata in a popup dialog"""
        if not self.current_file or not os.path.exists(self.current_file):
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.warning(self, "No File Loaded", "Please load an HDF5 file first.")
            return

        try:
            import h5py
            from PyQt6.QtWidgets import QDialog, QVBoxLayout, QTextEdit, QDialogButtonBox

            # Create dialog
            dialog = QDialog(self)
            dialog.setWindowTitle(f"HDF5 Metadata - {os.path.basename(self.current_file)}")
            dialog.resize(700, 500)

            layout = QVBoxLayout(dialog)

            # Text display
            text_edit = QTextEdit()
            text_edit.setReadOnly(True)
            text_edit.setFontFamily("Monospace")

            # Collect metadata
            metadata_text = []
            metadata_text.append(f"File: {self.current_file}\n")
            metadata_text.append(f"File Size: {os.path.getsize(self.current_file) / (1024*1024):.2f} MB\n")
            metadata_text.append("=" * 70 + "\n\n")

            with h5py.File(self.current_file, 'r') as f:
                # Root attributes
                if f.attrs:
                    metadata_text.append("ROOT ATTRIBUTES:\n")
                    metadata_text.append("-" * 70 + "\n")
                    for key, value in f.attrs.items():
                        metadata_text.append(f"  {key}: {value}\n")
                    metadata_text.append("\n")

                # EPROM Loads section
                if 'eprom_loads' in f:
                    metadata_text.append("\n")
                    metadata_text.append("EPROM LOADS:\n")
                    metadata_text.append("-" * 70 + "\n")

                    eprom_loads = f['eprom_loads'][:]

                    if len(eprom_loads) == 0:
                        metadata_text.append("  No EPROM loads recorded\n")
                    else:
                        for i, load in enumerate(eprom_loads):
                            # Decode name from bytes to string
                            name = load['name'].decode('utf-8').rstrip('\x00')
                            address = load['address']
                            length = load['length']
                            error_status = load['error_status']

                            metadata_text.append(f"\nEPROM Load #{i+1}:\n")
                            metadata_text.append(f"  Name:    {name}\n")
                            metadata_text.append(f"  Address: 0x{address:04X}\n")
                            metadata_text.append(f"  Length:  {length} bytes (0x{length:04X})\n")

                            if error_status == 0:
                                metadata_text.append(f"  Status:  Success\n")
                            else:
                                metadata_text.append(f"  Status:  Error (0x{error_status:02X})\n")
                    metadata_text.append("\n")
                
                # List all groups and datasets
                metadata_text.append("STRUCTURE:\n")
                metadata_text.append("-" * 70 + "\n")

                def visit_item(name, obj):
                    indent = "  " * (name.count('/'))
                    if isinstance(obj, h5py.Dataset):
                        metadata_text.append(f"{indent}📊 {name}\n")
                        metadata_text.append(f"{indent}   Shape: {obj.shape}\n")
                        metadata_text.append(f"{indent}   Dtype: {obj.dtype}\n")
                        if obj.attrs:
                            metadata_text.append(f"{indent}   Attributes:\n")
                            for key, value in obj.attrs.items():
                                metadata_text.append(f"{indent}     {key}: {value}\n")
                    elif isinstance(obj, h5py.Group):
                        metadata_text.append(f"{indent}📁 {name}/\n")
                        if obj.attrs:
                            metadata_text.append(f"{indent}   Attributes:\n")
                            for key, value in obj.attrs.items():
                                metadata_text.append(f"{indent}     {key}: {value}\n")

                f.visititems(visit_item)

            text_edit.setPlainText("".join(metadata_text))
            layout.addWidget(text_edit)

            # Buttons
            buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok)
            buttons.accepted.connect(dialog.accept)
            layout.addWidget(buttons)

            dialog.exec()

        except Exception as e:
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.critical(self, "Error", f"Failed to read metadata: {e}")

    def create_unified_dataframe(self, raw_data, stream_names):
        """DEPRECATED: No longer used - kept for reference only.

        Previously created a pre-resampled unified DataFrame, but this approach
        lost detail when zooming in. Now we use dynamic level-of-detail decimation
        directly from raw data in update_graph_plot().

        Args:
            raw_data: Dict of {stream_name: {'time': array, 'values': array}}
            stream_names: List of stream names to include

        Returns:
            pandas DataFrame with unified time axis
        """
        if not raw_data or not stream_names:
            return None

        # Find the time range across all streams
        all_times = []
        for stream in stream_names:
            if stream in raw_data:
                all_times.extend(raw_data[stream]['time'])

        if not all_times:
            return None

        time_min = min(all_times)
        time_max = max(all_times)

        # Create uniform time grid at 100 Hz (adjust based on data density)
        # This balances resolution vs. performance
        num_samples = int((time_max - time_min) * 100)
        num_samples = max(1000, min(num_samples, 100000))  # Clamp between 1k and 100k points

        uniform_time = np.linspace(time_min, time_max, num_samples)

        print(f"Creating unified time axis: {num_samples} samples from {time_min:.3f}s to {time_max:.3f}s")

        # Create DataFrame
        df_data = {'time': uniform_time}

        # Interpolate each stream onto the uniform time grid
        for stream in stream_names:
            if stream in raw_data:
                stream_data = raw_data[stream]
                # Use numpy interp for fast linear interpolation
                # Need to handle potential duplicate time values
                unique_indices = np.unique(stream_data['time'], return_index=True)[1]
                unique_time = stream_data['time'][unique_indices]
                unique_values = stream_data['values'][unique_indices]

                df_data[stream] = np.interp(
                    uniform_time,
                    unique_time,
                    unique_values
                )

        return pd.DataFrame(df_data)

    def toggle_stream(self, stream, state):
        """Handle stream enable/disable"""
        if state == Qt.CheckState.Checked.value:
            # Enable stream
            self.enabled_streams.append(stream)

            # Event streams don't own axes - they're just visual markers
            if stream in ['ecu_spark_x1', 'ecu_spark_x2', 'ecu_crankref_id', 'ecu_camshaft_timestamp']:
                self.update_graph_plot()
            # Assign ownership - but don't change the axis owner yet
            elif self.axis_owner is None:
                # No axis owner yet, this becomes the left axis
                self.axis_owner = stream
                # Don't call fit_axis_owner here!
                self.update_graph_plot()
            else:
                # Already have a left axis owner, move it to right and make new stream left
                self.right_axis_owner = self.axis_owner
                self.axis_owner = stream
                self.update_graph_plot()
            
        else:
            # Disable stream
            if stream in self.enabled_streams:
                self.enabled_streams.remove(stream)
            
            # Handle ownership reassignment
            if stream == self.axis_owner:
                # Disabling left axis owner
                if self.right_axis_owner and self.right_axis_owner in self.enabled_streams:
                    # Promote right axis owner to left
                    self.axis_owner = self.right_axis_owner
                    self.right_axis_owner = None
                    self.update_graph_plot()
                elif len(self.enabled_streams) > 0:
                    # Find next enabled stream for left axis (skip event marker streams)
                    stream_idx = self.data_streams.index(stream)
                    next_owner = None
                    event_streams = ['ecu_spark_x1', 'ecu_spark_x2', 'ecu_crankref_id', 'ecu_camshaft_timestamp']

                    # Search below
                    for i in range(stream_idx + 1, len(self.data_streams)):
                        candidate = self.data_streams[i]
                        if candidate in self.enabled_streams and candidate not in event_streams:
                            next_owner = candidate
                            break

                    # Wrap around if needed
                    if next_owner is None:
                        for i in range(0, stream_idx):
                            candidate = self.data_streams[i]
                            if candidate in self.enabled_streams and candidate not in event_streams:
                                next_owner = candidate
                                break

                    self.axis_owner = next_owner
                    if self.axis_owner:
                        self.update_graph_plot()
                else:
                    self.axis_owner = None
                    self.view_y_min = 0
                    self.view_y_max = 100
            elif stream == self.right_axis_owner:
                # Disabling right axis owner, just clear it
                self.right_axis_owner = None
            
        self.request_update()

    def on_stream_name_clicked(self, stream, event):
        """Handle clicking on stream name to change axis ownership"""
        # Event marker streams can't own axes
        if stream in ['ecu_spark_x1', 'ecu_spark_x2', 'ecu_crankref_id', 'ecu_camshaft_timestamp']:
            return

        if stream in self.enabled_streams:
            # Move current left axis owner to right, make clicked stream left owner
            if self.axis_owner and self.axis_owner != stream:
                self.right_axis_owner = self.axis_owner
            self.axis_owner = stream

            # Update the graph plot - no view range changes needed
            self.update_graph_plot()


    def request_update(self):
        """Request a plot update (rate-limited to 30 FPS)"""
        self.pending_update = True
        if not self.update_timer.isActive():
            self.update_timer.start()
    
    def perform_pending_update(self):
        """Perform the actual update"""
        if self.pending_update:
            self.update_graph_plot()
            self.update_navigation_plot()
            self.pending_update = False
        else:
            self.update_timer.stop()
    
    def update_graph_plot(self):
        """Update the main graph plot with dynamic level-of-detail"""
        self.graph_plot.clear()

        # Re-add GPS markers after clear (they get removed by clear())
        self.graph_plot.addItem(self.gps_markers)

        if not self.raw_data or len(self.enabled_streams) == 0:
            # Reset to default axis formatting when no streams are enabled
            left_axis = self.graph_plot.getAxis('left')
            # Remove custom tick value generator if it exists
            if hasattr(self, '_custom_tick_values'):
                left_axis.tickValues = left_axis.__class__.tickValues.__get__(left_axis, type(left_axis))
            # Remove custom tick string formatter if it exists
            if hasattr(left_axis, 'tickStrings') and hasattr(self, '_custom_tick_strings'):
                del left_axis.tickStrings
            self.graph_plot.setLabel('left', 'Value')
            self.graph_plot.setYRange(0, 100)
            return

        # Plot each enabled stream with dynamic decimation
        for stream in self.enabled_streams:
            if stream not in self.raw_data:
                continue

            # Skip event marker streams - they have custom visualization
            if stream in ['ecu_spark_x1', 'ecu_spark_x2', 'ecu_crankref_id', 'ecu_camshaft_timestamp']:
                continue

            color = self.stream_colors[stream]

            # Get raw data for this stream
            stream_data = self.raw_data[stream]
            all_time = stream_data['time']
            all_values = stream_data['values']

            # Filter to visible time window
            mask = (all_time >= self.view_start) & (all_time <= self.view_end)
            visible_time = all_time[mask]
            visible_values = all_values[mask]

            if len(visible_time) == 0:
                continue

            # Dynamic decimation based on zoom level
            # Target: ~10,000 points max for smooth rendering
            max_plot_points = 10000

            if len(visible_time) > max_plot_points:
                # Use min-max decimation to preserve peaks/valleys
                plot_time, plot_values = min_max_decimate(visible_time, visible_values, max_plot_points)
            else:
                # Zoomed in enough - plot every point
                plot_time = visible_time
                plot_values = visible_values

            # Get the stream's full data range for normalization
            stream_min, stream_max = self.stream_ranges.get(stream, (0, 1))

            # Normalize the data to 0-1 range
            normalized_data = (plot_values - stream_min) / (stream_max - stream_min)

            # Get display mode for this stream
            stream_widget = None
            for widget in self.stream_list_widget.stream_widgets:
                if widget.stream_name == stream:
                    stream_widget = widget
                    break

            if stream_widget and stream_widget.display_mode == "points":
                # Display as scatter points
                self.graph_plot.plot(
                    plot_time,
                    normalized_data,
                    pen=None,
                    symbol='o',
                    symbolSize=4,
                    symbolBrush=color,
                    symbolPen=None
                )
            else:
                # Display as line (default)
                pen = pg.mkPen(color=color, width=2)
                self.graph_plot.plot(plot_time, normalized_data, pen=pen)
        
        # Set axis properties based on owner (for display purposes only)
        if self.axis_owner and self.axis_owner in self.enabled_streams:
            color = self.stream_colors[self.axis_owner]
            self.graph_plot.getAxis('left').setPen(pg.mkPen(color=color, width=2))
            self.graph_plot.getAxis('left').setTextPen(pg.mkPen(color=color))
            
            # Set the y-axis label to the axis owner's name with larger font
            display_units = self.stream_metadata.get(self.axis_owner, {}).get('display_units', 'value')
            display_name = UnitConverter.get_display_name(self.axis_owner, 
                                                        self.stream_metadata.get(self.axis_owner, {}).get('native_units', 'value'), 
                                                        display_units)
            
            # Create font with configured size
            label_font = QFont()
            label_font.setPointSize(self.axis_font_size)
            
            # For pyqtgraph labels, we need to use the setLabel method properly
            self.graph_plot.setLabel('left', display_name)
            
            # Apply font to the axis label text
            self.graph_plot.getAxis('left').label.setFont(label_font)

            # Set larger font for tick labels as well
            tick_font = QFont()
            tick_font.setPointSize(self.axis_font_size)
            self.graph_plot.getAxis('left').setTickFont(tick_font)

        else:
            self.graph_plot.getAxis('left').setPen(pg.mkPen('k', width=2))
            self.graph_plot.getAxis('left').setTextPen(pg.mkPen('k'))
            # Use larger font for default "Value" label too
            label_font = QFont()
            label_font.setPointSize(self.axis_font_size)
            self.graph_plot.setLabel('left', 'Value')
            self.graph_plot.getAxis('left').label.setFont(label_font)

        # Set up right axis properties based on right axis owner
        if self.right_axis_owner and self.right_axis_owner in self.enabled_streams:
            color = self.stream_colors[self.right_axis_owner]
            self.graph_plot.getAxis('right').setPen(pg.mkPen(color=color, width=2))
            self.graph_plot.getAxis('right').setTextPen(pg.mkPen(color=color))

            # Set the right y-axis label to the axis owner's name with larger font
            display_units = self.stream_metadata.get(self.right_axis_owner, {}).get('display_units', 'value')
            display_name = UnitConverter.get_display_name(self.right_axis_owner,
                                                        self.stream_metadata.get(self.right_axis_owner, {}).get('native_units', 'value'),
                                                        display_units)

            # Create font with configured size
            label_font = QFont()
            label_font.setPointSize(self.axis_font_size)

            # For pyqtgraph labels, we need to use the setLabel method properly
            self.graph_plot.setLabel('right', display_name)

            # Apply font to the axis label text
            self.graph_plot.getAxis('right').label.setFont(label_font)

            # Set larger font for tick labels as well
            tick_font = QFont()
            tick_font.setPointSize(self.axis_font_size)
            self.graph_plot.getAxis('right').setTickFont(tick_font)

        else:
            self.graph_plot.getAxis('right').setPen(pg.mkPen('k', width=2))
            self.graph_plot.getAxis('right').setTextPen(pg.mkPen('k'))
            # Clear the right axis label when no owner
            label_font = QFont()
            label_font.setPointSize(self.axis_font_size)
            self.graph_plot.setLabel('right', '')
            self.graph_plot.getAxis('right').label.setFont(label_font)

        # Set x-axis label with configured font size
        x_label_font = QFont()
        x_label_font.setPointSize(self.axis_font_size)
        self.graph_plot.setLabel('bottom', 'Time (s)')
        self.graph_plot.getAxis('bottom').label.setFont(x_label_font)
        
        # Set x-axis tick labels with configured font size
        x_tick_font = QFont()
        x_tick_font.setPointSize(self.axis_font_size)
        self.graph_plot.getAxis('bottom').setTickFont(x_tick_font)

        # Set the actual Y range for display
        # Since all streams are now normalized to 0-1, always use 0-1 range
        # Extend slightly below 0 to show GPS markers at y=-0.05
        self.graph_plot.setXRange(self.view_start, self.view_end, padding=0)
        self.graph_plot.setYRange(-0.08, 1, padding=0)

        # Set up custom tick formatter to show axis owner's real values with round numbers
        if self.axis_owner and self.axis_owner in self.enabled_streams:
            axis_min, axis_max = self.stream_ranges.get(self.axis_owner, (0, 1))
            axis_range = axis_max - axis_min

            # Calculate nice round tick spacing
            import math
            def get_nice_tick_spacing(data_range):
                """Calculate a nice round number for tick spacing"""
                if data_range == 0:
                    return 1
                # Get order of magnitude
                exponent = math.floor(math.log10(data_range))
                magnitude = 10 ** exponent
                # Normalize to 1-10 range
                normalized = data_range / magnitude
                # Choose nice spacing: 0.1, 0.2, 0.5, 1, 2, 5, 10, etc.
                if normalized <= 1.0:
                    nice_spacing = 0.1 * magnitude
                elif normalized <= 2.0:
                    nice_spacing = 0.2 * magnitude
                elif normalized <= 5.0:
                    nice_spacing = 0.5 * magnitude
                elif normalized <= 10.0:
                    nice_spacing = 1.0 * magnitude
                else:
                    nice_spacing = 2.0 * magnitude
                return nice_spacing

            tick_spacing_real = get_nice_tick_spacing(axis_range)  # Calculate spacing based on full range

            # Round axis_min DOWN to nearest tick spacing multiple
            # This ensures ticks start at nice round numbers (0, 500, 1000, etc)
            axis_min_rounded = math.floor(axis_min / tick_spacing_real) * tick_spacing_real

            # Round axis_max UP to nearest tick spacing multiple
            axis_max_rounded = math.ceil(axis_max / tick_spacing_real) * tick_spacing_real

            # Generate nice round tick values in real units
            real_ticks = []
            tick_value = axis_min_rounded
            while tick_value <= axis_max_rounded:
                real_ticks.append(tick_value)
                tick_value += tick_spacing_real

            # DEBUG
            print(f"Stream: {self.axis_owner}")
            print(f"  Data range: {axis_min:.1f} to {axis_max:.1f}")
            print(f"  Rounded range: {axis_min_rounded:.1f} to {axis_max_rounded:.1f}")
            print(f"  Tick spacing: {tick_spacing_real:.1f}")
            print(f"  Real ticks: {real_ticks}")

            # Convert tick positions to DATA's normalized 0-1 space (where 0=axis_min, 1=axis_max)
            # This is where the ticks will actually be drawn since data is normalized to this range
            data_normalized_ticks = [(t - axis_min) / axis_range for t in real_ticks]
            print(f"  Normalized tick positions: {data_normalized_ticks}")

            left_axis = self.graph_plot.getAxis('left')

            # Filter ticks to only those within the 0-1 range (visible area)
            visible_ticks = [(norm_pos, real_val) for norm_pos, real_val in zip(data_normalized_ticks, real_ticks)
                           if 0 <= norm_pos <= 1]

            print(f"  Visible ticks: {visible_ticks}")

            # Override tickValues to specify exact tick positions
            def custom_tick_values(minVal, maxVal, size):
                # Return list of [(spacing, [tick_positions])] for major and minor ticks
                # We return our pre-calculated positions
                major_ticks = [pos for pos, _ in visible_ticks]
                minor_ticks = []  # No minor ticks

                print(f"  tickValues returning {len(major_ticks)} positions")
                return [(1.0, major_ticks), (0.0, minor_ticks)]

            self._custom_tick_values = custom_tick_values
            left_axis.tickValues = lambda minVal, maxVal, size: self._custom_tick_values(minVal, maxVal, size)

            # Override tickStrings to show real values
            tick_mapping = {pos: val for pos, val in visible_ticks}

            # Determine decimal precision based on tick spacing
            if tick_spacing_real >= 1:
                precision = 0  # No decimal places for spacing >= 1
            elif tick_spacing_real >= 0.1:
                precision = 1  # One decimal place for spacing like 0.1, 0.2, 0.5
            elif tick_spacing_real >= 0.01:
                precision = 2  # Two decimal places for spacing like 0.01, 0.02, 0.05
            else:
                precision = 3  # Three decimal places for very small spacing

            def custom_tick_strings(values, scale, spacing):
                print(f"  tickStrings called with {len(values)} positions: {values[:5]}")
                strings = []
                for v in values:
                    # Find closest tick in our mapping
                    closest = min(tick_mapping.keys(), key=lambda x: abs(x - v), default=None)
                    if closest is not None and abs(closest - v) < 0.001:
                        strings.append(f"{tick_mapping[closest]:.{precision}f}")
                    else:
                        # Shouldn't happen, but fallback
                        real_val = axis_min + v * axis_range
                        strings.append(f"{real_val:.{precision}f}")
                return strings

            self._custom_tick_strings = custom_tick_strings
            left_axis.tickStrings = lambda values, scale, spacing: self._custom_tick_strings(values, scale, spacing)
        else:
            # Reset to default tick formatting
            left_axis = self.graph_plot.getAxis('left')
            # Remove custom tick value generator if it exists
            if hasattr(self, '_custom_tick_values'):
                left_axis.tickValues = left_axis.__class__.tickValues.__get__(left_axis, type(left_axis))
                delattr(self, '_custom_tick_values')
            # Remove custom tick string formatter if it exists
            if hasattr(self, '_custom_tick_strings'):
                # Reset tickStrings to the default method from the class
                left_axis.tickStrings = left_axis.__class__.tickStrings.__get__(left_axis, type(left_axis))
                delattr(self, '_custom_tick_strings')

        # Set up custom tick formatter for RIGHT axis to show axis owner's real values with round numbers
        if self.right_axis_owner and self.right_axis_owner in self.enabled_streams:
            axis_min, axis_max = self.stream_ranges.get(self.right_axis_owner, (0, 1))
            axis_range = axis_max - axis_min

            # Calculate nice round tick spacing
            import math
            def get_nice_tick_spacing(data_range):
                """Calculate a nice round number for tick spacing"""
                if data_range == 0:
                    return 1
                # Get order of magnitude
                exponent = math.floor(math.log10(data_range))
                magnitude = 10 ** exponent
                # Normalize to 1-10 range
                normalized = data_range / magnitude
                # Choose nice spacing: 0.1, 0.2, 0.5, 1, 2, 5, 10, etc.
                if normalized <= 1.0:
                    nice_spacing = 0.1 * magnitude
                elif normalized <= 2.0:
                    nice_spacing = 0.2 * magnitude
                elif normalized <= 5.0:
                    nice_spacing = 0.5 * magnitude
                elif normalized <= 10.0:
                    nice_spacing = 1.0 * magnitude
                else:
                    nice_spacing = 2.0 * magnitude
                return nice_spacing

            tick_spacing_real = get_nice_tick_spacing(axis_range)  # Calculate spacing based on full range

            # Round axis_min DOWN to nearest tick spacing multiple
            # This ensures ticks start at nice round numbers (0, 500, 1000, etc)
            axis_min_rounded = math.floor(axis_min / tick_spacing_real) * tick_spacing_real

            # Round axis_max UP to nearest tick spacing multiple
            axis_max_rounded = math.ceil(axis_max / tick_spacing_real) * tick_spacing_real

            # Generate nice round tick values in real units
            real_ticks = []
            tick_value = axis_min_rounded
            while tick_value <= axis_max_rounded:
                real_ticks.append(tick_value)
                tick_value += tick_spacing_real

            # DEBUG
            print(f"Right Axis Stream: {self.right_axis_owner}")
            print(f"  Data range: {axis_min:.1f} to {axis_max:.1f}")
            print(f"  Rounded range: {axis_min_rounded:.1f} to {axis_max_rounded:.1f}")
            print(f"  Tick spacing: {tick_spacing_real:.1f}")
            print(f"  Real ticks: {real_ticks}")

            # Convert tick positions to DATA's normalized 0-1 space (where 0=axis_min, 1=axis_max)
            # This is where the ticks will actually be drawn since data is normalized to this range
            data_normalized_ticks = [(t - axis_min) / axis_range for t in real_ticks]
            print(f"  Normalized tick positions: {data_normalized_ticks}")

            right_axis = self.graph_plot.getAxis('right')

            # Filter ticks to only those within the 0-1 range (visible area)
            visible_ticks_right = [(norm_pos, real_val) for norm_pos, real_val in zip(data_normalized_ticks, real_ticks)
                           if 0 <= norm_pos <= 1]

            print(f"  Visible ticks: {visible_ticks_right}")

            # Override tickValues to specify exact tick positions
            def custom_tick_values_right(minVal, maxVal, size):
                # Return list of [(spacing, [tick_positions])] for major and minor ticks
                # We return our pre-calculated positions
                major_ticks = [pos for pos, _ in visible_ticks_right]
                minor_ticks = []  # No minor ticks

                print(f"  Right tickValues returning {len(major_ticks)} positions")
                return [(1.0, major_ticks), (0.0, minor_ticks)]

            self._custom_right_tick_values = custom_tick_values_right
            right_axis.tickValues = lambda minVal, maxVal, size: self._custom_right_tick_values(minVal, maxVal, size)

            # Override tickStrings to show real values
            tick_mapping_right = {pos: val for pos, val in visible_ticks_right}

            # Determine decimal precision based on tick spacing
            if tick_spacing_real >= 1:
                precision_right = 0  # No decimal places for spacing >= 1
            elif tick_spacing_real >= 0.1:
                precision_right = 1  # One decimal place for spacing like 0.1, 0.2, 0.5
            elif tick_spacing_real >= 0.01:
                precision_right = 2  # Two decimal places for spacing like 0.01, 0.02, 0.05
            else:
                precision_right = 3  # Three decimal places for very small spacing

            def custom_tick_strings_right(values, scale, spacing):
                print(f"  Right tickStrings called with {len(values)} positions: {values[:5]}")
                strings = []
                for v in values:
                    # Find closest tick in our mapping
                    closest = min(tick_mapping_right.keys(), key=lambda x: abs(x - v), default=None)
                    if closest is not None and abs(closest - v) < 0.001:
                        strings.append(f"{tick_mapping_right[closest]:.{precision_right}f}")
                    else:
                        # Shouldn't happen, but fallback
                        real_val = axis_min + v * axis_range
                        strings.append(f"{real_val:.{precision_right}f}")
                return strings

            self._custom_right_tick_strings = custom_tick_strings_right
            right_axis.tickStrings = lambda values, scale, spacing: self._custom_right_tick_strings(values, scale, spacing)
        else:
            # Reset to default tick formatting
            right_axis = self.graph_plot.getAxis('right')
            # Remove custom tick value generator if it exists
            if hasattr(self, '_custom_right_tick_values'):
                right_axis.tickValues = right_axis.__class__.tickValues.__get__(right_axis, type(right_axis))
                delattr(self, '_custom_right_tick_values')
            # Remove custom tick string formatter if it exists
            if hasattr(self, '_custom_right_tick_strings'):
                # Reset tickStrings to the default method from the class
                right_axis.tickStrings = right_axis.__class__.tickStrings.__get__(right_axis, type(right_axis))
                delattr(self, '_custom_right_tick_strings')

        # Update GPS position markers (triangles above time axis)
        if 'gps_position' in self.raw_data:
            gps_data = self.raw_data['gps_position']
            all_time = gps_data['time']

            # Filter to visible time window
            mask = (all_time >= self.view_start) & (all_time <= self.view_end)
            visible_indices = np.where(mask)[0]
            visible_time = all_time[mask]

            if len(visible_time) > 0:
                # Position markers just below y=0 (at -0.05 in normalized space)
                # This puts them just above the time axis
                y_positions = np.full(len(visible_time), -0.05)

                # Store the original indices in the data field (1D array of integers)
                # We'll use these to look up lat/lon when clicked
                # Clear existing points first
                self.gps_markers.clear()

                # Add points with clickable flag
                self.gps_markers.addPoints(
                    x=visible_time,
                    y=y_positions,
                    data=visible_indices  # Store indices for click events
                )
            else:
                # No GPS data in visible window, clear markers
                self.gps_markers.setData(x=[], y=[])
        else:
            # No GPS data loaded, clear markers
            self.gps_markers.setData(x=[], y=[])

        # Apply theme
        if self.dark_theme:
            self.graph_plot.setBackground('#2b2b2b')
            self.graph_plot.getAxis('bottom').setPen('w')
            self.graph_plot.getAxis('bottom').setTextPen('w')
            # Only apply theme to left axis if there's no left axis owner
            # (if there is an owner, the owner's color was already set above)
            if not (self.axis_owner and self.axis_owner in self.enabled_streams):
                self.graph_plot.getAxis('left').setPen('w')
                self.graph_plot.getAxis('left').setTextPen('w')
            # Only apply theme to right axis if there's no right axis owner
            # (if there is an owner, the owner's color was already set above)
            if not (self.right_axis_owner and self.right_axis_owner in self.enabled_streams):
                self.graph_plot.getAxis('right').setPen('w')
                self.graph_plot.getAxis('right').setTextPen('w')
            # Show grid only for X axis and left Y axis to avoid duplicate horizontal grid lines
            self.graph_plot.showGrid(x=True, y=True, alpha=0.5)
            # Disable grid for right axis to prevent duplicate horizontal lines
            self.graph_plot.getAxis('right').setGrid(False)
        else:
            self.graph_plot.setBackground('w')
            self.graph_plot.getAxis('bottom').setPen('k')
            self.graph_plot.getAxis('bottom').setTextPen('k')
            # Only apply theme to left axis if there's no left axis owner
            # (if there is an owner, the owner's color was already set above)
            if not (self.axis_owner and self.axis_owner in self.enabled_streams):
                self.graph_plot.getAxis('left').setPen('k')
                self.graph_plot.getAxis('left').setTextPen('k')
            # Only apply theme to right axis if there's no right axis owner
            # (if there is an owner, the owner's color was already set above)
            if not (self.right_axis_owner and self.right_axis_owner in self.enabled_streams):
                self.graph_plot.getAxis('right').setPen('k')
                self.graph_plot.getAxis('right').setTextPen('k')
            # Show grid only for X axis and left Y axis to avoid duplicate horizontal grid lines
            self.graph_plot.showGrid(x=True, y=True, alpha=0.3)
            # Disable grid for right axis to prevent duplicate horizontal lines
            self.graph_plot.getAxis('right').setGrid(False)

        # Draw event markers if ecu_rpm_instantaneous is available
        self.draw_spark_events()
        self.draw_crankref_events()
        self.draw_camshaft_events()

    def draw_spark_events(self):
        """Draw spark event markers (x1/x2) on the graph, positioned relative to RPM"""
        # Only draw if we have instantaneous RPM data
        if 'ecu_rpm_instantaneous' not in self.raw_data:
            print("DEBUG: No ecu_rpm_instantaneous data for spark events")
            return

        rpm_data = self.raw_data['ecu_rpm_instantaneous']
        rpm_time = rpm_data['time']
        rpm_values = rpm_data['values']

        # Get RPM normalization range
        rpm_min, rpm_max = self.stream_ranges.get('ecu_rpm_instantaneous', (0, 1))
        print(f"DEBUG draw_spark_events: RPM range {rpm_min:.1f} to {rpm_max:.1f}")

        # Process each spark type
        spark_streams = [
            ('ecu_spark_x1', 'S1', SPARK_LABEL_OFFSET),   # x1 above
            ('ecu_spark_x2', 'S2', -SPARK_LABEL_OFFSET)  # x2 below
        ]

        for spark_name, label_text, offset in spark_streams:
            # Only draw if this spark stream is enabled
            if spark_name not in self.enabled_streams:
                print(f"DEBUG: {spark_name} not in enabled_streams")
                continue

            if spark_name not in self.raw_data:
                print(f"DEBUG: {spark_name} not in raw_data")
                continue

            spark_data = self.raw_data[spark_name]
            spark_times = spark_data['time']
            print(f"DEBUG: {spark_name} has {len(spark_times)} events, time range {spark_times[0]:.2f} to {spark_times[-1]:.2f}")

            # Get color from stream colors if available
            color = self.stream_colors.get(spark_name, '#FF0000')  # Default to red

            # Filter to visible time window
            mask = (spark_times >= self.view_start) & (spark_times <= self.view_end)
            visible_spark_times = spark_times[mask]
            print(f"DEBUG: {spark_name} has {len(visible_spark_times)} visible events in window {self.view_start:.2f} to {self.view_end:.2f}")

            for spark_time in visible_spark_times:
                # Interpolate RPM value at spark time
                if spark_time < rpm_time[0] or spark_time > rpm_time[-1]:
                    continue  # Skip if spark is outside RPM data range

                # Linear interpolation of RPM at spark time
                rpm_at_spark = np.interp(spark_time, rpm_time, rpm_values)

                # Normalize RPM value to 0-1 range (same as plot data)
                normalized_rpm = (rpm_at_spark - rpm_min) / (rpm_max - rpm_min)

                # Calculate label position (above or below RPM line)
                label_y = normalized_rpm + offset

                # Draw vertical line from label to RPM point
                line_item = pg.PlotDataItem(
                    [spark_time, spark_time],
                    [label_y, normalized_rpm],
                    pen=pg.mkPen(color=color, width=2.0, style=Qt.PenStyle.SolidLine)
                )
                self.graph_plot.addItem(line_item)

                # Add text label at offset position
                text_item = pg.TextItem(text=label_text, color=color, anchor=(0.5, 0.5))
                text_item.setPos(spark_time, label_y)
                self.graph_plot.addItem(text_item)

    def draw_crankref_events(self):
        """Draw crankref event markers on the graph, positioned relative to RPM"""
        # Only draw if we have instantaneous RPM data
        if 'ecu_rpm_instantaneous' not in self.raw_data:
            return

        # Only draw if crankref stream is enabled
        if 'ecu_crankref_id' not in self.enabled_streams:
            return

        if 'ecu_crankref_id' not in self.raw_data:
            return

        rpm_data = self.raw_data['ecu_rpm_instantaneous']
        rpm_time = rpm_data['time']
        rpm_values = rpm_data['values']

        # Get RPM normalization range
        rpm_min, rpm_max = self.stream_ranges.get('ecu_rpm_instantaneous', (0, 1))

        crankref_data = self.raw_data['ecu_crankref_id']
        crankref_times = crankref_data['time']
        crankref_ids = crankref_data['values']

        # Get color for crankref stream
        color = self.stream_colors.get('ecu_crankref_id', '#00FF00')  # Default to green

        # Filter to visible time window
        mask = (crankref_times >= self.view_start) & (crankref_times <= self.view_end)
        visible_crankref_times = crankref_times[mask]
        visible_crankref_ids = crankref_ids[mask]

        for crankref_time, crankref_id in zip(visible_crankref_times, visible_crankref_ids):
            # Interpolate RPM value at crankref time
            if crankref_time < rpm_time[0] or crankref_time > rpm_time[-1]:
                continue  # Skip if crankref is outside RPM data range

            # Linear interpolation of RPM at crankref time
            rpm_at_crankref = np.interp(crankref_time, rpm_time, rpm_values)

            # Normalize RPM value to 0-1 range (same as plot data)
            normalized_rpm = (rpm_at_crankref - rpm_min) / (rpm_max - rpm_min)

            # Calculate label position (above the RPM line)
            label_y = normalized_rpm + CRANKREF_LINE_HEIGHT

            # Draw vertical line upward from RPM point
            line_item = pg.PlotDataItem(
                [crankref_time, crankref_time],
                [normalized_rpm, label_y],
                pen=pg.mkPen(color=color, width=2.0, style=Qt.PenStyle.SolidLine)
            )
            self.graph_plot.addItem(line_item)

            # Add text label above the line
            label_text = f"CR{int(crankref_id)}"
            text_item = pg.TextItem(text=label_text, color=color, anchor=(0.5, 1.0))  # anchor bottom center
            text_item.setPos(crankref_time, label_y)
            self.graph_plot.addItem(text_item)

    def draw_camshaft_events(self):
        """Draw camshaft event markers on the graph, positioned relative to RPM"""
        # Only draw if we have instantaneous RPM data
        if 'ecu_rpm_instantaneous' not in self.raw_data:
            return

        # Only draw if camshaft stream is enabled
        if 'ecu_camshaft_timestamp' not in self.enabled_streams:
            return

        if 'ecu_camshaft_timestamp' not in self.raw_data:
            return

        rpm_data = self.raw_data['ecu_rpm_instantaneous']
        rpm_time = rpm_data['time']
        rpm_values = rpm_data['values']

        # Get RPM normalization range
        rpm_min, rpm_max = self.stream_ranges.get('ecu_rpm_instantaneous', (0, 1))

        camshaft_data = self.raw_data['ecu_camshaft_timestamp']
        camshaft_times = camshaft_data['time']

        # Get color for camshaft stream
        color = self.stream_colors.get('ecu_camshaft_timestamp', '#FF00FF')  # Default to magenta

        # Filter to visible time window
        mask = (camshaft_times >= self.view_start) & (camshaft_times <= self.view_end)
        visible_camshaft_times = camshaft_times[mask]

        for camshaft_time in visible_camshaft_times:
            # Interpolate RPM value at camshaft time
            if camshaft_time < rpm_time[0] or camshaft_time > rpm_time[-1]:
                continue  # Skip if camshaft is outside RPM data range

            # Linear interpolation of RPM at camshaft time
            rpm_at_camshaft = np.interp(camshaft_time, rpm_time, rpm_values)

            # Normalize RPM value to 0-1 range (same as plot data)
            normalized_rpm = (rpm_at_camshaft - rpm_min) / (rpm_max - rpm_min)

            # Calculate label position (below the RPM line - downward direction)
            label_y = normalized_rpm - CAMSHAFT_LINE_HEIGHT

            # Draw vertical line downward from RPM point
            line_item = pg.PlotDataItem(
                [camshaft_time, camshaft_time],
                [normalized_rpm, label_y],
                pen=pg.mkPen(color=color, width=2.0, style=Qt.PenStyle.SolidLine)
            )
            self.graph_plot.addItem(line_item)

            # Add text label below the line
            label_text = "CAM"
            text_item = pg.TextItem(text=label_text, color=color, anchor=(0.5, 0.0))  # anchor top center
            text_item.setPos(camshaft_time, label_y)
            self.graph_plot.addItem(text_item)

    def update_navigation_plot(self):
        """Update the navigation plot with decimated overview"""
        self.nav_plot.clear()
        self.nav_plot.addItem(self.view_region)

        if not self.raw_data or len(self.enabled_streams) == 0:
            return

        self.nav_plot.setXRange(0, self.total_time_span, padding=0)
        # Set Y range to 0-1 since all streams are normalized
        self.nav_plot.setYRange(0, 1, padding=0)

        # Plot enabled streams with heavy decimation for overview
        max_nav_points = 1000
        for stream in self.enabled_streams:
            if stream not in self.raw_data:
                continue

            # Skip event marker streams - they're not plotted in navigation view
            if stream in ['ecu_spark_x1', 'ecu_spark_x2', 'ecu_crankref_id', 'ecu_camshaft_timestamp']:
                continue

            stream_data = self.raw_data[stream]
            all_time = stream_data['time']
            all_values = stream_data['values']

            # Decimate for navigation view
            if len(all_time) > max_nav_points:
                nav_time, nav_values = min_max_decimate(all_time, all_values, max_nav_points)
            else:
                nav_time = all_time
                nav_values = all_values

            # Normalize to 0-1 range like main graph
            stream_min, stream_max = self.stream_ranges.get(stream, (0, 1))
            normalized_values = (nav_values - stream_min) / (stream_max - stream_min)

            color = self.stream_colors[stream]
            pen = pg.mkPen(color=color, width=1)
            self.nav_plot.plot(nav_time, normalized_values, pen=pen)
        
        # Apply theme
        if self.dark_theme:
            self.nav_plot.setBackground('#2b2b2b')
            self.nav_plot.getAxis('bottom').setPen('w')
            self.nav_plot.getAxis('bottom').setTextPen('w')
        else:
            self.nav_plot.setBackground('w')
            self.nav_plot.getAxis('bottom').setPen('k')
            self.nav_plot.getAxis('bottom').setTextPen('k')
    
    def on_region_changed(self):
        """Handle navigation region changes (real-time)"""
        region = self.view_region.getRegion()
        new_start, new_end = region
        
        # Clamp
        view_duration = new_end - new_start
        
        if view_duration < 0.1:
            view_duration = 0.1
            new_end = new_start + view_duration
        
        if new_start < 0:
            new_start = 0
            new_end = view_duration
        
        if new_end > self.total_time_span:
            new_end = self.total_time_span
            new_start = max(0, new_end - view_duration)
        
        if new_start != region[0] or new_end != region[1]:
            self.view_region.blockSignals(True)
            self.view_region.setRegion([new_start, new_end])
            self.view_region.blockSignals(False)
        
        self.view_start = new_start
        self.view_end = new_end
        
        self.request_update()
    
    def on_region_change_finished(self):
        """Handle navigation region change completion"""
        # Add to history when user finishes dragging (Y is always 0-1, so no need to store)
        self.add_to_history(self.view_start, self.view_end, 0, 1)

    
    def handle_graph_zoom(self, x_min, x_max, y_min, y_max):
        """Handle rubber band zoom in graph - only X-axis zoom, Y is always 0-1"""
        self.add_to_history(self.view_start, self.view_end, 0, 1)

        # Only zoom on X-axis (time), ignore Y since streams are independently scaled
        self.view_start = max(0, x_min)
        self.view_end = min(self.total_time_span, x_max)

        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)

        self.update_graph_plot()
    
    def fit_axis_owner(self):
        """No-op: Each stream is always shown at its full natural scale"""
        # With normalization, each stream is always displayed at its full range (0-1 normalized)
        # Just refresh the plot to update axis labels if needed
        self.update_graph_plot()

    def fit_all(self):
        """No-op: Each stream is always shown at its full natural scale"""
        # With normalization, each stream is always displayed at its full range (0-1 normalized)
        # Just refresh the plot
        self.update_graph_plot()
    
    def reset_view(self):
        """Reset view to initial state"""
        self.view_start = self.initial_view_start
        self.view_end = self.initial_view_end
        # No need to reset view_y_min/max since Y is always 0-1

        self.update_graph_plot()

        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)

        self.history = []
        self.history_index = -1
        self.update_history_buttons()

        self.update_graph_plot()

    def zoom_out_2x(self):
        """Zoom out by 2x (double the time shown), centered on current view"""
        # Add current view to history
        self.add_to_history(self.view_start, self.view_end, 0, 1)

        # Calculate current view duration and center
        current_duration = self.view_end - self.view_start
        center = (self.view_start + self.view_end) / 2

        # Double the duration
        new_duration = current_duration * 2

        # Calculate new start/end centered on the same point
        new_start = center - new_duration / 2
        new_end = center + new_duration / 2

        # Clamp to valid range
        new_start = max(0, new_start)
        new_end = min(self.total_time_span, new_end)

        # If we hit a boundary, adjust the other side to maintain 2x zoom if possible
        if new_start == 0:
            new_end = min(new_duration, self.total_time_span)
        elif new_end == self.total_time_span:
            new_start = max(0, self.total_time_span - new_duration)

        self.view_start = new_start
        self.view_end = new_end

        # Update navigation region
        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)

        self.update_graph_plot()

    def pan_left_50(self):
        """Pan left by 50% of current view duration"""
        # Add current view to history
        self.add_to_history(self.view_start, self.view_end, 0, 1)

        # Calculate 50% of current duration
        current_duration = self.view_end - self.view_start
        shift = current_duration * 0.5

        # Shift left (decrease both start and end)
        new_start = self.view_start - shift
        new_end = self.view_end - shift

        # Clamp to valid range
        if new_start < 0:
            new_start = 0
            new_end = current_duration

        self.view_start = new_start
        self.view_end = new_end

        # Update navigation region
        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)

        self.update_graph_plot()

    def pan_right_50(self):
        """Pan right by 50% of current view duration"""
        # Add current view to history
        self.add_to_history(self.view_start, self.view_end, 0, 1)

        # Calculate 50% of current duration
        current_duration = self.view_end - self.view_start
        shift = current_duration * 0.5

        # Shift right (increase both start and end)
        new_start = self.view_start + shift
        new_end = self.view_end + shift

        # Clamp to valid range
        if new_end > self.total_time_span:
            new_end = self.total_time_span
            new_start = self.total_time_span - current_duration

        self.view_start = new_start
        self.view_end = new_end

        # Update navigation region
        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)

        self.update_graph_plot()

    
    def add_to_history(self, start, end, y_min, y_max):
        """Add state to history (only X/time axis, Y is always 0-1)"""
        self.history = self.history[:self.history_index + 1]
        # Still store 4 values for compatibility, but y_min/y_max are ignored (always 0, 1)
        self.history.append((start, end, 0, 1))
        self.history_index += 1

        if len(self.history) > 50:
            self.history.pop(0)
            self.history_index -= 1

        self.update_history_buttons()

    def undo(self):
        """Undo last operation"""
        if self.history_index > 0:
            self.history_index -= 1
            start, end, _, _ = self.history[self.history_index]  # Ignore y_min, y_max

            self.view_start = start
            self.view_end = end
            # No need to restore Y range since it's always 0-1

            self.view_region.blockSignals(True)
            self.view_region.setRegion([start, end])
            self.view_region.blockSignals(False)

            self.update_graph_plot()
            self.update_history_buttons()

    def redo(self):
        """Redo previously undone operation"""
        if self.history_index < len(self.history) - 1:
            self.history_index += 1
            start, end, _, _ = self.history[self.history_index]  # Ignore y_min, y_max

            self.view_start = start
            self.view_end = end
            # No need to restore Y range since it's always 0-1

            self.view_region.blockSignals(True)
            self.view_region.setRegion([start, end])
            self.view_region.blockSignals(False)

            self.update_graph_plot()
            self.update_history_buttons()
    
    def update_history_buttons(self):
        """Update undo/redo action states"""
        self.undo_action.setEnabled(self.history_index > 0)
        self.redo_action.setEnabled(self.history_index < len(self.history) - 1)

    def apply_window_border(self):
        """Apply window border based on theme"""
        if self.dark_theme:
            # Light border for dark theme
            self.setStyleSheet("QMainWindow { border: 2px solid #666666; }")
        else:
            # Dark border for light theme
            self.setStyleSheet("QMainWindow { border: 2px solid #333333; }")

    def set_theme(self, dark):
        """Set theme (True for dark, False for light)"""
        if self.dark_theme == dark:
            return  # No change

        self.dark_theme = dark

        # Update checkmarks
        self.light_theme_action.setChecked(not dark)
        self.dark_theme_action.setChecked(dark)

        # Save theme preference
        self.config.set("theme", "dark" if dark else "light")

        # Apply window border
        self.apply_window_border()

        # Update panel backgrounds
        if self.dark_theme:
            # Dark theme backgrounds
            self.stream_selection.setStyleSheet("QWidget { background-color: #3b3b3b; }")
            self.nav_widget.setStyleSheet("QWidget { background-color: #3b3b3b; }")
        else:
            # Light theme backgrounds
            self.stream_selection.setStyleSheet("QWidget { background-color: white; }")
            self.nav_widget.setStyleSheet("QWidget { background-color: white; }")

        # Update all stream widgets with new theme
        for widget in self.stream_list_widget.stream_widgets:
            widget.set_theme(self.dark_theme)

        # Update all plots
        self.update_graph_plot()
        self.update_navigation_plot()

    def toggle_theme(self):
        """Toggle between light and dark themes (kept for backward compatibility)"""
        self.set_theme(not self.dark_theme)

    def show_ride_on_maps(self):
        """Show the entire GPS track on a map (triggered from menu)"""
        # Call the GPS marker handler with no specific clicked point
        # We'll use the first GPS point as the "clicked" point for centering
        if 'gps_position' not in self.raw_data:
            print("No GPS data available")
            return

        gps_data = self.raw_data['gps_position']
        if len(gps_data['time']) == 0:
            print("No GPS data available")
            return

        # Create a fake clicked point using the middle of the track
        middle_index = len(gps_data['time']) // 2

        # Create a mock points object with the middle point
        class MockPoint:
            def __init__(self, index):
                self._data = index
            def data(self):
                return self._data

        mock_points = [MockPoint(middle_index)]

        # Call the existing handler
        self.on_gps_marker_clicked(None, mock_points)

    def on_gps_marker_clicked(self, scatter_plot_item, points):
        """Handle GPS marker click events - show all GPS points on one map

        Note: Due to pyqtgraph design, this requires double-click"""
        import subprocess
        import shutil
        import tempfile
        import os
        import json

        if 'gps_position' not in self.raw_data:
            print("No GPS data available")
            return

        gps_data = self.raw_data['gps_position']

        # Get the clicked point info
        clicked_index = int(points[0].data())
        clicked_time = gps_data['time'][clicked_index]
        clicked_lat = gps_data['lat'][clicked_index]
        clicked_lon = gps_data['lon'][clicked_index]

        print(f"GPS marker clicked at time={clicked_time:.3f}s, lat={clicked_lat:.6f}, lon={clicked_lon:.6f}")
        print(f"Generating map with all {len(gps_data['time'])} GPS positions...")

        # Create a list of all GPS positions for the map
        positions = []
        for i in range(len(gps_data['time'])):
            positions.append({
                'lat': float(gps_data['lat'][i]),
                'lon': float(gps_data['lon'][i]),
                'time': float(gps_data['time'][i]),
                'clicked': i == clicked_index
            })

        # Create HTML with Leaflet.js (open source map library)
        temp_dir = tempfile.gettempdir()
        html_file = os.path.join(temp_dir, 'umod4_gps_track.html')

        html_content = f"""<!DOCTYPE html>
<html>
<head>
    <title>UMOD4 GPS Track</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
    <style>
        body {{ margin: 0; padding: 0; }}
        #map {{ width: 100%; height: 100vh; }}
        .info {{
            padding: 6px 8px;
            background: white;
            box-shadow: 0 0 15px rgba(0,0,0,0.2);
            border-radius: 5px;
        }}
        .info h4 {{ margin: 0 0 5px; color: #777; }}
    </style>
</head>
<body>
    <div id="map"></div>
    <script>
        // GPS positions data
        const positions = {json.dumps(positions)};

        // Find clicked position
        const clickedPos = positions.find(p => p.clicked);

        // Create map centered on clicked position
        const map = L.map('map').setView([clickedPos.lat, clickedPos.lon], 15);

        // Add OpenStreetMap tiles
        L.tileLayer('https://{{s}}.tile.openstreetmap.org/{{z}}/{{x}}/{{y}}.png', {{
            attribution: '&copy; OpenStreetMap contributors',
            maxZoom: 19
        }}).addTo(map);

        // Create polyline for the track
        const trackPoints = positions.map(p => [p.lat, p.lon]);
        L.polyline(trackPoints, {{
            color: 'blue',
            weight: 3,
            opacity: 0.7
        }}).addTo(map);

        // Add markers for each position
        positions.forEach((pos, idx) => {{
            const marker = L.circleMarker([pos.lat, pos.lon], {{
                radius: pos.clicked ? 8 : 4,
                fillColor: pos.clicked ? '#ff0000' : '#0066ff',
                color: pos.clicked ? '#cc0000' : '#0044cc',
                weight: 2,
                opacity: 1,
                fillOpacity: pos.clicked ? 1 : 0.6
            }}).addTo(map);

            marker.bindPopup(
                `<b>Point ${{idx + 1}}</b><br>` +
                `Time: ${{pos.time.toFixed(3)}}s<br>` +
                `Lat: ${{pos.lat.toFixed(6)}}<br>` +
                `Lon: ${{pos.lon.toFixed(6)}}`
            );

            if (pos.clicked) {{
                marker.openPopup();
            }}
        }});

        // Add info box
        const info = L.control({{position: 'topright'}});
        info.onAdd = function(map) {{
            this._div = L.DomUtil.create('div', 'info');
            this._div.innerHTML =
                '<h4>UMOD4 GPS Track</h4>' +
                `<b>${{positions.length}}</b> GPS positions<br>` +
                `Clicked: Time ${{clickedPos.time.toFixed(3)}}s`;
            return this._div;
        }};
        info.addTo(map);

        // Fit map to show all points
        const bounds = L.latLngBounds(trackPoints);
        map.fitBounds(bounds, {{padding: [50, 50]}});
    </script>
</body>
</html>"""

        with open(html_file, 'w') as f:
            f.write(html_content)

        # Open the map in browser
        try:
            if shutil.which('wslview'):
                subprocess.Popen(['wslview', html_file])
            elif shutil.which('powershell.exe'):
                result = subprocess.run(['wslpath', '-w', html_file],
                                      capture_output=True, text=True)
                windows_path = result.stdout.strip()
                print(f"Opening GPS track map at: {windows_path}")
                subprocess.Popen(['powershell.exe', '-Command', 'Start-Process', f'"{windows_path}"'])
            else:
                import webbrowser
                file_url = f'file:///{html_file.replace(os.sep, "/")}'
                webbrowser.open(file_url, new=2)
        except Exception as e:
            print(f"Error opening browser: {e}")
            print(f"Map file: {html_file}")

    def closeEvent(self, event):
        """Handle window close event - save configuration."""
        # Save window geometry and splitter positions
        self.config.save_window_geometry(self)
        self.config.save_splitter_state("horizontal", self.h_splitter)
        self.config.save_splitter_state("vertical", self.v_splitter)

        # Accept the close event
        super().closeEvent(event)
    
    def on_splitter_moved(self, pos, index):
        """Handle splitter movement"""
        if self.raw_data:
            self.update_graph_plot()

def main():
    # Parse command-line arguments
    parser = argparse.ArgumentParser(
        description='Data Visualization Tool for HDF5 log files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                    # Open without loading a file
  %(prog)s logfile.h5         # Open and load specified log file
  %(prog)s /path/to/data.h5   # Open with absolute path
        """
    )
    parser.add_argument(
        'logfile',
        nargs='?',
        default=None,
        help='HDF5 log file to open (optional)'
    )
    args = parser.parse_args()

    app = QApplication(sys.argv)
    app.setStyle('Fusion')

    # Set organization and application name for QSettings
    QApplication.setOrganizationName("umod4")
    QApplication.setApplicationName("LogVisualizer")

    window = DataVisualizationTool()
    window.show()

    # Load file if specified on command line
    if args.logfile:
        if os.path.exists(args.logfile):
            window.load_hdf5_file_internal(args.logfile)
        else:
            print(f"Error: File not found: {args.logfile}")

    sys.exit(app.exec())

if __name__ == '__main__':
    main()
