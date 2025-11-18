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

    # Each bin contributes 2 points (min and max)
    # So we need target_points/2 bins
    num_bins = max(1, target_points // 2)
    bin_size = max(1, n // num_bins)

    result_time = []
    result_values = []

    for i in range(0, n, bin_size):
        bin_time = time_array[i:i+bin_size]
        bin_values = value_array[i:i+bin_size]

        if len(bin_values) == 0:
            continue

        # Find min and max indices within this bin
        min_idx = bin_values.argmin()
        max_idx = bin_values.argmax()

        # Add both points in time order to maintain monotonic time axis
        if min_idx < max_idx:
            result_time.extend([bin_time[min_idx], bin_time[max_idx]])
            result_values.extend([bin_values[min_idx], bin_values[max_idx]])
        else:
            result_time.extend([bin_time[max_idx], bin_time[min_idx]])
            result_values.extend([bin_values[max_idx], bin_values[min_idx]])

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
        self.axis_owner = None
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
        
        # Create ribbon
        self.create_ribbon()
        main_layout.addWidget(self.ribbon)
        
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
        
    def create_ribbon(self):
        """Create the ribbon with controls"""
        self.ribbon = QWidget()
        self.ribbon.setFixedHeight(40)
        self.ribbon.setStyleSheet("QWidget { background-color: #f0f0f0; }")

        ribbon_layout = QHBoxLayout(self.ribbon)
        ribbon_layout.setContentsMargins(5, 5, 5, 5)

        # Data menu
        data_menu_btn = QPushButton("Data ▼")
        data_menu = QMenu()

        load_action = QAction("Load HDF5 Log File", self)
        load_action.triggered.connect(self.load_hdf5_file)
        load_action.setEnabled(HDF5_AVAILABLE)
        if not HDF5_AVAILABLE:
            load_action.setText("Load HDF5 Log File (h5py not installed)")
        data_menu.addAction(load_action)

        # Add recent files submenu
        self.recent_files_menu = QMenu("Recent Files")
        self.update_recent_files_menu()
        data_menu.addMenu(self.recent_files_menu)

        # Add metadata viewer
        data_menu.addSeparator()
        metadata_action = QAction("View HDF5 Metadata", self)
        metadata_action.triggered.connect(self.show_metadata_dialog)
        metadata_action.setEnabled(HDF5_AVAILABLE)
        data_menu.addAction(metadata_action)

        data_menu_btn.setMenu(data_menu)
        ribbon_layout.addWidget(data_menu_btn)
        
        # Undo/Redo buttons
        self.undo_btn = QPushButton("Undo")
        self.undo_btn.clicked.connect(self.undo)
        self.undo_btn.setEnabled(False)
        ribbon_layout.addWidget(self.undo_btn)
        
        self.redo_btn = QPushButton("Redo")
        self.redo_btn.clicked.connect(self.redo)
        self.redo_btn.setEnabled(False)
        ribbon_layout.addWidget(self.redo_btn)
        
        # Reset button
        reset_btn = QPushButton("Reset View")
        reset_btn.clicked.connect(self.reset_view)
        ribbon_layout.addWidget(reset_btn)
        
        # Fit button
        fit_btn = QPushButton("Fit")
        fit_btn.clicked.connect(self.fit_axis_owner)
        ribbon_layout.addWidget(fit_btn)
        
        # Fit All button
        fit_all_btn = QPushButton("Fit All")
        fit_all_btn.clicked.connect(self.fit_all)
        ribbon_layout.addWidget(fit_all_btn)
        
        # Theme toggle button
        self.theme_btn = QPushButton("Dark Theme" if not self.dark_theme else "Light Theme")
        self.theme_btn.clicked.connect(self.toggle_theme)
        ribbon_layout.addWidget(self.theme_btn)
        
        # Font size control (optional)
        font_size_label = QLabel("Font Size:")
        ribbon_layout.addWidget(font_size_label)
        
        self.font_size_spinbox = QSpinBox()
        self.font_size_spinbox.setRange(8, 24)
        self.font_size_spinbox.setValue(self.axis_font_size)
        self.font_size_spinbox.valueChanged.connect(self.change_font_size)
        ribbon_layout.addWidget(self.font_size_spinbox)
        
        ribbon_layout.addStretch()
        
        # Version label (right-aligned)
        version_label = QLabel("Data Visualization Tool v1.1")
        version_label.setStyleSheet("font-weight: bold;")
        ribbon_layout.addWidget(version_label)

    def change_font_size(self, size):
        """Change the axis font size and stream label font size"""
        self.axis_font_size = size
        self.config.set("axis_font_size", size)

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
    
        # Ensure font sizes are set properly for initial state
        label_font = QFont()
        label_font.setPointSize(self.axis_font_size)
        self.graph_plot.getAxis('bottom').setTickFont(label_font)
        self.graph_plot.getAxis('left').setTickFont(label_font)
        
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

            stream_widget.checkbox.stateChanged.connect(
                lambda state, s=stream: self.toggle_stream(s, state)
            )
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
                            # Split into separate streams
                            if key == 'gps_position':
                                raw_data['gps_latitude'] = {
                                    'time': time_ns,
                                    'values': ds[:, 1]
                                }
                                raw_data['gps_longitude'] = {
                                    'time': time_ns,
                                    'values': ds[:, 2]
                                }
                                stream_names.extend(['gps_latitude', 'gps_longitude'])

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
            
            # Assign ownership - but don't change the axis owner yet
            if self.axis_owner is None:
                self.axis_owner = stream
                # Don't call fit_axis_owner here!
                self.update_graph_plot()
            
        else:
            # Disable stream
            if stream in self.enabled_streams:
                self.enabled_streams.remove(stream)
            
            # Handle ownership reassignment
            if stream == self.axis_owner:
                if len(self.enabled_streams) > 0:
                    # Find next enabled stream
                    stream_idx = self.data_streams.index(stream)
                    next_owner = None
                    
                    # Search below
                    for i in range(stream_idx + 1, len(self.data_streams)):
                        if self.data_streams[i] in self.enabled_streams:
                            next_owner = self.data_streams[i]
                            break
                    
                    # Wrap around if needed
                    if next_owner is None:
                        for i in range(0, stream_idx):
                            if self.data_streams[i] in self.enabled_streams:
                                next_owner = self.data_streams[i]
                                break
                    
                    self.axis_owner = next_owner
                    if self.axis_owner:
                        # Don't call fit_axis_owner here!
                        self.update_graph_plot()
                else:
                    self.axis_owner = None
                    self.view_y_min = 0
                    self.view_y_max = 100
            
        self.request_update()

    def on_stream_name_clicked(self, stream, event):
        """Handle clicking on stream name to change axis ownership"""
        if stream in self.enabled_streams:
            # Set new axis owner (this will only affect axis labeling)
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
        self.graph_plot.setXRange(self.view_start, self.view_end, padding=0)
        self.graph_plot.setYRange(0, 1, padding=0)

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
                # Choose nice spacing (1, 2, 5, or 10)
                if normalized <= 1.5:
                    nice_spacing = 0.2 * magnitude
                elif normalized <= 3:
                    nice_spacing = 0.5 * magnitude
                elif normalized <= 7:
                    nice_spacing = 1.0 * magnitude
                else:
                    nice_spacing = 2.0 * magnitude
                return nice_spacing

            tick_spacing_real = get_nice_tick_spacing(axis_range / 5)  # Aim for ~5 ticks

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

            def custom_tick_strings(values, scale, spacing):
                print(f"  tickStrings called with {len(values)} positions: {values[:5]}")
                strings = []
                for v in values:
                    # Find closest tick in our mapping
                    closest = min(tick_mapping.keys(), key=lambda x: abs(x - v), default=None)
                    if closest is not None and abs(closest - v) < 0.001:
                        strings.append(f"{tick_mapping[closest]:.0f}")
                    else:
                        # Shouldn't happen, but fallback
                        real_val = axis_min + v * axis_range
                        strings.append(f"{real_val:.0f}")
                return strings

            self._custom_tick_strings = custom_tick_strings
            left_axis.tickStrings = lambda values, scale, spacing: self._custom_tick_strings(values, scale, spacing)
        else:
            # Reset to default tick formatting
            left_axis = self.graph_plot.getAxis('left')
            # Remove custom tick value generator if it exists
            if hasattr(self, '_custom_tick_values'):
                left_axis.tickValues = left_axis.__class__.tickValues.__get__(left_axis, type(left_axis))
            # Remove custom tick string formatter if it exists
            if hasattr(left_axis, 'tickStrings') and hasattr(self, '_custom_tick_strings'):
                del left_axis.tickStrings
        
        # Apply theme
        if self.dark_theme:
            self.graph_plot.setBackground('#2b2b2b')
            self.graph_plot.getAxis('bottom').setPen('w')
            self.graph_plot.getAxis('left').setPen('w')
            self.graph_plot.getAxis('bottom').setTextPen('w')
            self.graph_plot.getAxis('left').setTextPen('w')
            self.graph_plot.showGrid(x=True, y=True, alpha=0.5)
        else:
            self.graph_plot.setBackground('w')
            self.graph_plot.getAxis('bottom').setPen('k')
            self.graph_plot.getAxis('left').setPen('k')
            self.graph_plot.getAxis('bottom').setTextPen('k')
            self.graph_plot.getAxis('left').setTextPen('k')
            self.graph_plot.showGrid(x=True, y=True, alpha=0.3)

    def update_navigation_plot(self):
        """Update the navigation plot with decimated overview"""
        self.nav_plot.clear()
        self.nav_plot.addItem(self.view_region)

        if not self.raw_data or len(self.enabled_streams) == 0:
            return

        self.nav_plot.setXRange(0, self.total_time_span, padding=0)

        # Plot enabled streams with heavy decimation for overview
        max_nav_points = 1000
        for stream in self.enabled_streams:
            if stream not in self.raw_data:
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

            color = self.stream_colors[stream]
            pen = pg.mkPen(color=color, width=1)
            self.nav_plot.plot(nav_time, nav_values, pen=pen)
        
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
        """Update undo/redo button states"""
        self.undo_btn.setEnabled(self.history_index > 0)
        self.redo_btn.setEnabled(self.history_index < len(self.history) - 1)
    
    def toggle_theme(self):
        """Toggle between light and dark themes"""
        self.dark_theme = not self.dark_theme

        # Save theme preference
        self.config.set("theme", "dark" if self.dark_theme else "light")

        if self.dark_theme:
            # Apply dark theme
            self.ribbon.setStyleSheet("QWidget { background-color: #3b3b3b; }")
            self.stream_selection.setStyleSheet("QWidget { background-color: #3b3b3b; }")
            self.nav_widget.setStyleSheet("QWidget { background-color: #3b3b3b; }")
            self.theme_btn.setText("Light Theme")
        else:
            # Apply light theme
            self.ribbon.setStyleSheet("QWidget { background-color: #f0f0f0; }")
            self.stream_selection.setStyleSheet("QWidget { background-color: white; }")
            self.nav_widget.setStyleSheet("QWidget { background-color: white; }")
            self.theme_btn.setText("Dark Theme")

        # Update all stream widgets with new theme
        for widget in self.stream_list_widget.stream_widgets:
            widget.set_theme(self.dark_theme)

        # Update all plots
        self.update_graph_plot()
        self.update_navigation_plot()

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
