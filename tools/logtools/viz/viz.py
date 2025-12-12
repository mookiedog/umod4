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

# Must have h5py for HDF5 file support
import h5py
HDF5_AVAILABLE = True
#try:
#    import h5py
#    HDF5_AVAILABLE = True
#except ImportError:
#    HDF5_AVAILABLE = False
#    h5py = None

# Import stream configuration system
from stream_config import get_config_manager, StreamType

# Import viz_components modules
from viz_components.config import AppConfig, UnitConverter, PerFileSettingsManager, PerFileSettings
from viz_components.widgets import (ColorCheckbox, StreamCheckbox, DraggableStreamList,
                                     ZoomableGraphWidget, ResizableSplitter)
from viz_components.utils import parse_color_to_rgba
from viz_components.rendering import min_max_decimate, DataNormalizer
from viz_components.data import HDF5DataLoader, DataManager
from viz_components.navigation import ViewNavigationController, ViewHistory

# Import decoder for .um4 file conversion
# Try to import directly first (works in Nuitka onefile builds where modules are bundled)
DECODER_IMPORT_ERROR = None
try:
    import decodelog
    DECODER_AVAILABLE = True
except ImportError as e:
    DECODER_IMPORT_ERROR = str(e)
    # Fallback: add decoder directory to path (works in development/source runs)
    decoder_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../decoder')
    if os.path.exists(decoder_path):
        sys.path.insert(0, decoder_path)
        try:
            import decodelog
            DECODER_AVAILABLE = True
            DECODER_IMPORT_ERROR = None
        except ImportError as e2:
            DECODER_AVAILABLE = False
            decodelog = None
            DECODER_IMPORT_ERROR = f"Path tried: {decoder_path}\nError: {str(e2)}"
    else:
        DECODER_AVAILABLE = False
        decodelog = None
        DECODER_IMPORT_ERROR = f"Decoder path does not exist: {decoder_path}"

# Event visualization constants
SPARK_LABEL_OFFSET = 0.025  # Vertical offset from RPM line for spark labels (2.5% of normalized range)
CRANKREF_LINE_HEIGHT = 0.04  # Height of vertical line for crankref markers (4% of normalized range)
CAMSHAFT_LINE_HEIGHT = 0.04  # Height of vertical line for camshaft markers (4% of normalized range)
MAX_VISIBLE_EVENT_MARKERS = 100  # Skip drawing event markers if more than this many are visible

# Injector bar visualization constants
FRONT_INJECTOR_BAR_Y = 0.25  # Fixed Y position for front injector bars (normalized)
REAR_INJECTOR_BAR_Y = 0.15  # Fixed Y position for rear injector bars (normalized)
INJECTOR_BAR_HEIGHT = 0.05  # Height of injector bars (normalized)
INJECTOR_BAR_ALPHA = 0.7  # Transparency of injector bars (0-1)
TIMER_TICK_DURATION_S = 2e-6  # ECU timer tick duration: 2 microseconds
MAX_INJECTOR_BARS_VISIBLE = 5000  # Maximum bars to draw for performance
MAX_REASONABLE_DURATION_TICKS = 50000  # 100ms max reasonable duration (startup injector pulses can be long)


class InjectorBarItem(QtWidgets.QGraphicsRectItem):
    """Custom rectangle item for injector bars with proper hover detection"""

    def __init__(self, x, y, width, height, tooltip_text, color, alpha):
        # Create rect at (0, 0) with the specified size
        super().__init__(0, 0, width, height)
        # Then set the item's position in the scene
        self.setPos(x, y)

        self._tooltip_text = tooltip_text
        self.setAcceptHoverEvents(True)

        # Set up appearance - use NO PEN to avoid bounding box issues
        self.setPen(pg.mkPen(None))

        rgba = parse_color_to_rgba(color, alpha)
        self.setBrush(pg.mkBrush(*rgba))

    def hoverEnterEvent(self, event):
        """Show tooltip only when mouse enters the actual bar rectangle"""
        self.setToolTip(self._tooltip_text)
        super().hoverEnterEvent(event)

    def hoverLeaveEvent(self, event):
        """Clear tooltip when mouse leaves"""
        self.setToolTip("")
        super().hoverLeaveEvent(event)

    def shape(self):
        """Override shape to return exact rectangle bounds for hover detection"""
        path = QtWidgets.QPainterPath()
        path.addRect(self.rect())
        return path

# Axis tick spacing constraints
MIN_TEMPERATURE_TICK_SPACING_C = 5.0  # Minimum spacing between temperature axis ticks (degrees C)

# ================================================================================================
# Main Application Class
# ================================================================================================

class DataVisualizationTool(QMainWindow):
    def __init__(self, debug=False):
        super().__init__()

        # Debug flag for verbose output
        self.debug = debug

        # Initialize configuration managers FIRST
        self.config = AppConfig()
        self.stream_config = get_config_manager()
        self.per_file_settings_manager = PerFileSettingsManager(debug=self.debug)

        # Print config location for user awareness
        self.debug_print(f"Configuration file: {self.config.settings.fileName()}")
        self.debug_print(f"Stream configuration: {self.stream_config.config_path}")

        self.base_title = "Data Visualization Tool - v1.1"
        self.setWindowTitle(self.base_title)
        self.setGeometry(100, 100, 1600, 900)

        # Data management (Phase 2 refactoring)
        self.hdf5_loader = HDF5DataLoader(self.stream_config)
        self.data_manager = DataManager()

        # Legacy data accessors (for backward compatibility during refactoring)
        # TODO: Phase 4+ will remove these in favor of data_manager methods
        self.raw_data = {}  # Will point to data_manager.raw_data
        self.data_streams = []  # Will point to data_manager.stream_names
        self.stream_metadata = {}  # Will point to data_manager.stream_metadata
        self.stream_ranges = {}  # Will point to data_manager.stream_ranges

        # Navigation management (Phase 3 refactoring)
        self.view_controller = ViewNavigationController()
        self.view_history = ViewHistory(max_history=50)

        # Rendering management (Phase 4 refactoring - partial)
        # Normalizer will be initialized after data is loaded (needs stream_ranges)
        self.normalizer = None

        # Connect view history signal to update UI buttons
        self.view_history.history_changed.connect(self._on_history_changed)

        # Legacy view state accessors (for backward compatibility during refactoring)
        # TODO: Phase 5+ will remove these in favor of view_controller methods
        self.view_start = 0
        self.view_end = self.config.get("default_view_duration")
        self.view_y_min = 0
        self.view_y_max = 100
        self.total_time_span = 0
        self.initial_view_start = 0
        self.initial_view_end = self.config.get("default_view_duration")
        self.time_min = 0
        self.time_max = 0

        # Legacy history accessors (for backward compatibility)
        self.history = []
        self.history_index = -1

        # UI state
        self.stream_colors = {}
        self.enabled_streams = []
        self.axis_owner = None  # Stream that owns the left Y-axis
        self.right_axis_owner = None  # Stream that owns the right Y-axis
        self.current_file = None  # Track currently loaded file
        self._pending_per_file_settings = None  # Settings loaded from .viz file, applied during populate_stream_selection()

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

    def debug_print(self, *args, **kwargs):
        """Print debug message if debug flag is enabled"""
        if self.debug:
            print(*args, **kwargs)

    def _on_history_changed(self, can_undo, can_redo):
        """Callback when history state changes - update UI buttons"""
        if hasattr(self, 'undo_action'):  # Check if UI is initialized
            self.undo_action.setEnabled(can_undo)
            self.redo_action.setEnabled(can_redo)

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
        load_action = QAction("&Load File...", self)
        load_action.setShortcut("Ctrl+O")
        load_action.triggered.connect(self.load_hdf5_file)
        load_action.setEnabled(HDF5_AVAILABLE)
        if not HDF5_AVAILABLE:
            load_action.setText("&Load File... (h5py not installed)")
        file_menu.addAction(load_action)

        # Recent files submenu
        self.recent_files_menu = file_menu.addMenu("Recent Files")
        self.update_recent_files_menu()

        file_menu.addSeparator()

        # Save Layout action
        save_layout_action = QAction("&Save Layout", self)
        save_layout_action.setShortcut("Ctrl+S")
        save_layout_action.triggered.connect(self.save_layout_manual)
        file_menu.addAction(save_layout_action)

        # Reset Layout action
        reset_layout_action = QAction("&Reset Layout", self)
        reset_layout_action.triggered.connect(self.reset_layout)
        file_menu.addAction(reset_layout_action)

        # Import Layout action
        import_layout_action = QAction("&Import Layout...", self)
        import_layout_action.triggered.connect(self.import_layout)
        file_menu.addAction(import_layout_action)

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
        self.undo_action.setShortcut("Alt+Left")
        self.undo_action.triggered.connect(self.undo)
        self.undo_action.setEnabled(False)
        view_menu.addAction(self.undo_action)

        # Redo action
        self.redo_action = QAction("&Redo", self)
        self.redo_action.setShortcut("Alt+Right")
        self.redo_action.triggered.connect(self.redo)
        self.redo_action.setEnabled(False)
        view_menu.addAction(self.redo_action)

        view_menu.addSeparator()

        # Zoom In 2x
        zoom_in_action = QAction("Zoom &In 2x", self)
        zoom_in_action.setShortcut("Ctrl++")
        zoom_in_action.triggered.connect(self.zoom_in_2x)
        view_menu.addAction(zoom_in_action)

        # Zoom Out 2x
        zoom_out_action = QAction("Zoom &Out 2x", self)
        zoom_out_action.setShortcut("Ctrl+-")
        zoom_out_action.triggered.connect(self.zoom_out_2x)
        view_menu.addAction(zoom_out_action)

        # Pan Left 50%
        pan_left_action = QAction("Pan &Left 50%", self)
        pan_left_action.setShortcut("Shift+Left")
        pan_left_action.triggered.connect(self.pan_left_50)
        view_menu.addAction(pan_left_action)

        # Pan Right 50%
        pan_right_action = QAction("Pan Right 50%", self)
        pan_right_action.setShortcut("Shift+Right")
        pan_right_action.triggered.connect(self.pan_right_50)
        view_menu.addAction(pan_right_action)

        # Pan Left 15%
        pan_left_15_action = QAction("Pan Left 15%", self)
        pan_left_15_action.setShortcut("Ctrl+Left")
        pan_left_15_action.triggered.connect(self.pan_left_15)
        view_menu.addAction(pan_left_15_action)

        # Pan Right 15%
        pan_right_15_action = QAction("Pan Right 15%", self)
        pan_right_15_action.setShortcut("Ctrl+Right")
        pan_right_15_action.triggered.connect(self.pan_right_15)
        view_menu.addAction(pan_right_15_action)

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

        # Disable automatic SI prefix scaling (prevents "(x0.001)" annotations)
        self.graph_plot.getAxis('left').enableAutoSIPrefix(False)
        self.graph_plot.getAxis('right').enableAutoSIPrefix(False)
        self.graph_plot.getAxis('bottom').enableAutoSIPrefix(False)

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

        self.nav_plot = ZoomableGraphWidget()
        self.nav_plot.setBackground('w')
        self.nav_plot.setLabel('bottom', 'Time (s)')
        self.nav_plot.hideAxis('left')
        self.nav_plot.getPlotItem().setMouseEnabled(x=False, y=False)

        # Add linear region for view selection
        self.view_region = pg.LinearRegionItem(
            [0, 10],
            brush=pg.mkBrush(100, 150, 255, 100),
            pen=pg.mkPen('b', width=2),
            movable=True
        )
        self.view_region.setZValue(10)

        # Disable edge resizing - only allow dragging the entire box
        # The rubber band zoom is the preferred way to resize the view
        for line in self.view_region.lines:
            line.setMovable(False)

        self.nav_plot.addItem(self.view_region)
        self.view_region.sigRegionChanged.connect(self.on_region_changed)
        self.view_region.sigRegionChangeFinished.connect(self.on_region_change_finished)

        # Set zoom callback for rubber band zoom
        self.nav_plot.zoom_callback = self.on_nav_zoom

        # Exclude the navigation region from rubber band zoom - clicking inside
        # the blue box should drag it, not start a rubber band zoom
        self.nav_plot.exclude_region = self.view_region

        nav_layout.addWidget(self.nav_plot)
        
    def populate_stream_selection(self):
        """Populate the stream selection window"""
        # Clear existing
        self.stream_list_widget.clear_streams()
        self.stream_colors.clear()

        # Get stream order: per-file settings → global QSettings → default
        per_file_settings = self._pending_per_file_settings

        if per_file_settings and per_file_settings.stream_order:
            # Use per-file order
            display_order = [s for s in per_file_settings.stream_order if s in self.data_streams]
            # Add any new streams not in saved order
            for stream in self.data_streams:
                if stream not in display_order:
                    display_order.append(stream)
            self.debug_print(f"Using stream order from .viz file")
        else:
            # Fall back to global settings or YAML config order
            saved_order = self.config.get_stream_order()
            if saved_order:
                # Filter to only include streams that exist in current file
                display_order = [s for s in saved_order if s in self.data_streams]
                # Add any new streams not in saved order
                for stream in self.data_streams:
                    if stream not in display_order:
                        display_order.append(stream)
                self.debug_print(f"Using stream order from global settings")
            else:
                # Use order from stream_config.yaml
                config_mgr = get_config_manager()
                yaml_order = list(config_mgr.streams.keys())
                # Filter to only include streams that exist in current file
                display_order = [s for s in yaml_order if s in self.data_streams]
                # Add any new streams not in YAML (shouldn't happen, but be safe)
                for stream in self.data_streams:
                    if stream not in display_order:
                        display_order.append(stream)
                self.debug_print(f"Using stream order from stream_config.yaml")

        # Create checkboxes for each stream in display order
        for i, stream in enumerate(display_order):
            color = self.colors[i % len(self.colors)]
            self.stream_colors[stream] = color

            # Get display name from config, fallback to stream name
            stream_config = self.stream_config.get_stream(stream)
            display_name = stream_config.display_name if stream_config else stream

            stream_widget = StreamCheckbox(stream, color, display_name=display_name)
            stream_widget.set_theme(self.dark_theme)  # Initialize theme
            stream_widget.set_font_size(self.axis_font_size)  # Initialize font size

            # Set up callbacks
            stream_widget.color_change_callback = self.on_stream_color_changed
            stream_widget.display_mode_callback = self.on_stream_display_mode_changed

            # Load saved preferences: per-file → global QSettings → YAML default → color cycle
            # Color precedence
            if per_file_settings and stream in per_file_settings.stream_colors:
                saved_color = per_file_settings.stream_colors[stream]
                stream_widget.color = saved_color
                stream_widget.checkbox.fill_color = saved_color
                self.stream_colors[stream] = saved_color
            else:
                # Check global QSettings
                saved_color = self.config.get(f"stream_color_{stream}")
                if saved_color:
                    stream_widget.color = saved_color
                    stream_widget.checkbox.fill_color = saved_color
                    self.stream_colors[stream] = saved_color
                else:
                    # Check YAML default
                    stream_config = self.stream_config.get_stream(stream)
                    if stream_config and hasattr(stream_config, 'color') and stream_config.color:
                        stream_widget.color = stream_config.color
                        stream_widget.checkbox.fill_color = stream_config.color
                        self.stream_colors[stream] = stream_config.color

            # Display mode precedence
            if per_file_settings and stream in per_file_settings.stream_display_modes:
                saved_mode = per_file_settings.stream_display_modes[stream]
            else:
                saved_mode = self.config.get(f"stream_display_mode_{stream}", "line")
            stream_widget.display_mode = saved_mode

            # Connect signal before setting default state so it fires
            stream_widget.checkbox.stateChanged.connect(
                lambda state, s=stream: self.toggle_stream(s, state)
            )

            stream_widget.label.mousePressEvent = lambda ev, s=stream: self.on_stream_name_clicked(s, ev)

            self.stream_list_widget.add_stream_widget(stream_widget)

        # Enable streams and restore axis ownership
        if per_file_settings and per_file_settings.enabled_streams:
            # Use per-file enabled streams
            self.debug_print(f"Enabling streams from .viz: {per_file_settings.enabled_streams}")
            for stream_name in per_file_settings.enabled_streams:
                for widget in self.stream_list_widget.stream_widgets:
                    if widget.stream_name == stream_name:
                        widget.checkbox.setChecked(True)
                        break

            # Restore axis ownership
            if per_file_settings.axis_owner:
                self.axis_owner = per_file_settings.axis_owner
                self.debug_print(f"Restored left axis owner: {self.axis_owner}")
            if per_file_settings.right_axis_owner:
                self.right_axis_owner = per_file_settings.right_axis_owner
                self.debug_print(f"Restored right axis owner: {self.right_axis_owner}")
        else:
            # Enable default streams AFTER all widgets are created
            # Order matters: coolant temp (left), throttle ADC (right), RPM (left)
            # This results in: RPM owns left axis, throttle ADC owns right axis, coolant temp enabled
            default_streams = [
                "ecu_coolant_temp_c",     # First: becomes left axis owner
                "ecu_throttle_adc",       # Second: moves first to right, becomes left (will be moved to right by third)
                "ecu_rpm_instantaneous"   # Third: moves second to right, becomes left axis owner
            ]

            for stream_name in default_streams:
                # Find the widget for this stream
                for widget in self.stream_list_widget.stream_widgets:
                    if widget.stream_name == stream_name:
                        widget.checkbox.setChecked(True)
                        break

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
            "Open Log File",
            start_dir,
            "Log Files (*.h5 *.hdf5 *.um4);;HDF5 Files (*.h5 *.hdf5);;Binary Logs (*.um4);;All Files (*)"
        )

        if filename:
            self.load_file_with_conversion(filename)

    def load_file_with_conversion(self, filename):
        """Load a file, offering to convert from .um4 to .h5 if needed."""
        from PyQt6.QtWidgets import QMessageBox
        import subprocess

        # Check if file is HDF5
        if filename.endswith('.h5') or filename.endswith('.hdf5'):
            self.load_hdf5_file_internal(filename)
            return

        # File is not HDF5 - offer to convert
        if filename.endswith('.um4'):
            # Check if corresponding .h5 file already exists
            suggested_h5 = os.path.splitext(filename)[0] + '.h5'

            if os.path.exists(suggested_h5):
                # .h5 file exists - ask user what to do
                reply = QMessageBox.question(
                    self,
                    "HDF5 File Exists",
                    f"Found existing HDF5 file:\n{os.path.basename(suggested_h5)}\n\n"
                    f"Do you want to:\n"
                    f"  • YES: Load the existing .h5 file\n"
                    f"  • NO: Recreate it from {os.path.basename(filename)}",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No | QMessageBox.StandardButton.Cancel,
                    QMessageBox.StandardButton.Yes
                )

                if reply == QMessageBox.StandardButton.Cancel:
                    return
                elif reply == QMessageBox.StandardButton.Yes:
                    # Load existing .h5 file
                    self.load_hdf5_file_internal(suggested_h5)
                    return
                # If No, fall through to conversion
            else:
                # No .h5 file exists - ask if they want to convert
                reply = QMessageBox.question(
                    self,
                    "Convert Binary Log",
                    f"This is a binary log file (.um4).\n\n"
                    f"Would you like to convert it to HDF5 format?\n\n"
                    f"This will run: decodelog.py {os.path.basename(filename)} --format h5",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                    QMessageBox.StandardButton.Yes
                )

                if reply == QMessageBox.StandardButton.No:
                    return

            # Ask user where to save the .h5 file
            h5_filename, _ = QFileDialog.getSaveFileName(
                self,
                "Save Converted HDF5 File",
                suggested_h5,
                "HDF5 Files (*.h5);;All Files (*)"
            )

            if not h5_filename:
                return

            # Run decoder to convert .um4 to .h5
            try:
                if not DECODER_AVAILABLE or decodelog is None:
                    error_msg = "The decoder module is not available.\n\n"
                    if DECODER_IMPORT_ERROR:
                        error_msg += f"Import error:\n{DECODER_IMPORT_ERROR}\n\n"
                    error_msg += "Cannot convert .um4 files to HDF5 format."
                    QMessageBox.critical(
                        self,
                        "Decoder Not Available",
                        error_msg,
                        QMessageBox.StandardButton.Ok
                    )
                    return

                # Show progress message
                QMessageBox.information(
                    self,
                    "Converting...",
                    f"Converting {os.path.basename(filename)} to HDF5...\n\n"
                    f"This may take a moment. Click OK to continue.",
                    QMessageBox.StandardButton.Ok
                )

                # Save original sys.argv and replace it with our arguments
                original_argv = sys.argv
                sys.argv = ['decodelog', filename, '--format', 'h5', '-o', h5_filename]

                try:
                    # Call the decoder's main function directly
                    decodelog.main()

                    # Success - load the converted file
                    QMessageBox.information(
                        self,
                        "Conversion Complete",
                        f"Successfully converted to:\n{h5_filename}",
                        QMessageBox.StandardButton.Ok
                    )
                    self.load_hdf5_file_internal(h5_filename)

                finally:
                    # Restore original sys.argv
                    sys.argv = original_argv

            except Exception as e:
                QMessageBox.critical(
                    self,
                    "Conversion Error",
                    f"Error converting file:\n\n{str(e)}",
                    QMessageBox.StandardButton.Ok
                )
        else:
            # Unknown file type
            QMessageBox.warning(
                self,
                "Unknown File Type",
                f"File must be either .h5/.hdf5 or .um4\n\nSelected: {os.path.basename(filename)}",
                QMessageBox.StandardButton.Ok
            )


    def load_hdf5_file_internal(self, filename):
        """Internal method to load HDF5 file (used by both file picker and recent files)."""
        try:
            self.current_file = filename

            # Load data using HDF5DataLoader (Phase 2 refactoring)
            loaded_data = self.hdf5_loader.load_file(filename, self.config)

            # Store data in DataManager
            self.data_manager.set_data(
                loaded_data['raw_data'],
                loaded_data['stream_names'],
                loaded_data['stream_ranges'],
                loaded_data['stream_metadata'],
                loaded_data['file_metadata'],
                loaded_data['time_bounds']
            )

            # Update legacy accessors (backward compatibility)
            self.raw_data = self.data_manager.raw_data
            self.data_streams = self.data_manager.stream_names
            self.stream_metadata = self.data_manager.stream_metadata
            self.stream_ranges = self.data_manager.stream_ranges

            # Set time bounds
            time_min, time_max = self.data_manager.get_time_bounds()
            self.total_time_span = self.data_manager.time_span
            self.time_min = time_min
            self.time_max = time_max

            # Initialize view controller with time bounds
            self.view_controller.set_time_bounds(time_min, time_max)

            # Load per-file settings if available
            per_file_settings = self.per_file_settings_manager.load(filename)
            if per_file_settings:
                # Validate streams against actual file contents
                per_file_settings = self.per_file_settings_manager.validate_against_file(
                    per_file_settings,
                    self.data_streams
                )
                # Validate and clamp view bounds
                per_file_settings = self.per_file_settings_manager.validate_view_bounds(
                    per_file_settings,
                    time_min,
                    time_max
                )
                self._pending_per_file_settings = per_file_settings
                self.debug_print(f"Loaded per-file settings from .viz file")
            else:
                self._pending_per_file_settings = None
                self.debug_print(f"No .viz file found, using defaults")

            # Calculate initial view window (may be overridden by per-file settings)
            # Note: If view_history exists, we'll use the current entry from history instead
            if per_file_settings and per_file_settings.view_start is not None and per_file_settings.view_end is not None:
                self.view_start = per_file_settings.view_start
                self.view_end = per_file_settings.view_end
                self.debug_print(f"Using saved view from .viz: [{self.view_start:.2f}s, {self.view_end:.2f}s]")
            else:
                self.view_start, self.view_end = self.data_manager.calculate_initial_view(
                    self.stream_config,
                    self.config.get("default_view_duration", 10.0)
                )
                self.debug_print(f"Calculated initial view: [{self.view_start:.2f}s, {self.view_end:.2f}s]")

            # Store the initial view (for the "reset zoom" to full data range)
            # If restoring from history, this might get overridden below
            self.initial_view_start = self.view_start
            self.initial_view_end = self.view_end

            # Set initial view in controller (may be updated after history restoration)
            self.view_controller.set_initial_view(self.view_start, self.view_end)
            self.view_controller.set_view_range(self.view_start, self.view_end)

            # Initialize normalizer (Phase 4 refactoring)
            self.normalizer = DataNormalizer(self.stream_config, self.stream_ranges, debug=self.debug)

            self.debug_print(f"Initial view: [{self.view_start:.2f}s, {self.view_end:.2f}s]")

            # Reset state
            self.enabled_streams = []
            self.axis_owner = None
            self.view_history.clear()

            self.populate_stream_selection()
            self.update_navigation_plot()
            self.update_graph_plot()

            # Restore view history from per-file settings or add initial view
            if per_file_settings and per_file_settings.view_history:
                history_data = per_file_settings.view_history
                self.view_history.history = [tuple(entry) for entry in history_data.get('history', [])]
                self.view_history.history_index = history_data.get('current_index', 0)
                self.view_history._emit_state()  # Update UI buttons
                self.debug_print(f"Restored view history: {len(self.view_history.history)} entries, index={self.view_history.history_index}")

                # Ensure the displayed view matches the current history entry
                # The history[current_index] should be the view we're displaying
                if self.view_history.history and 0 <= self.view_history.history_index < len(self.view_history.history):
                    current_view = self.view_history.history[self.view_history.history_index]
                    self.view_start, self.view_end = current_view[0], current_view[1]
                    self.view_controller.set_view_range(self.view_start, self.view_end)
                    self.debug_print(f"Set view to history[{self.view_history.history_index}]: [{self.view_start:.2f}s, {self.view_end:.2f}s]")

                    # Update plots to show the correct view
                    self.update_navigation_plot()
                    self.update_graph_plot()
            else:
                # Add initial view to history
                self.view_history.push(self.view_start, self.view_end, 0, 1)

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

        self.debug_print(f"Creating unified time axis: {num_samples} samples from {time_min:.3f}s to {time_max:.3f}s")

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
            if self.stream_config.is_event_only_stream(stream):
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

                    # Search below
                    for i in range(stream_idx + 1, len(self.data_streams)):
                        candidate = self.data_streams[i]
                        if candidate in self.enabled_streams and not self.stream_config.is_event_only_stream(candidate):
                            next_owner = candidate
                            break

                    # Wrap around if needed
                    if next_owner is None:
                        for i in range(0, stream_idx):
                            candidate = self.data_streams[i]
                            if candidate in self.enabled_streams and not self.stream_config.is_event_only_stream(candidate):
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
        if self.stream_config.is_event_only_stream(stream):
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
        import math  # For temperature range calculations
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

        # First pass: calculate visible range for axis owner (needed for normalization)
        # Use DataNormalizer (Phase 4 refactoring)
        axis_owner_visible_range = None
        if self.normalizer and self.axis_owner and self.axis_owner in self.enabled_streams:
            axis_owner_visible_range = self.normalizer.calculate_axis_owner_range(
                self.axis_owner,
                self.raw_data,
                self.view_start,
                self.view_end
            )

        # Plot each enabled stream with dynamic decimation
        for stream in self.enabled_streams:
            if stream not in self.raw_data:
                continue

            # Skip event-only streams (markers, bars, etc.) - they have custom visualization
            if self.stream_config.is_event_only_stream(stream):
                continue

            # Skip duration/helper streams that shouldn't be plotted
            if self.stream_config.should_skip_in_selection(stream):
                continue

            color = self.stream_colors[stream]

            # Get raw data for this stream
            stream_data = self.raw_data[stream]
            all_time = stream_data['time']
            all_values = stream_data['values']

            # Filter to visible time window, including one point on each edge
            # This ensures lines connect properly even at extreme zoom levels
            mask = (all_time >= self.view_start) & (all_time <= self.view_end)
            visible_indices = np.where(mask)[0]

            if len(visible_indices) == 0:
                # No points in view - find closest points on either side
                left_idx = np.searchsorted(all_time, self.view_start, side='right') - 1
                right_idx = np.searchsorted(all_time, self.view_end, side='left')

                if left_idx >= 0 and right_idx < len(all_time):
                    # Points exist on both sides
                    visible_indices = np.array([left_idx, right_idx])
                elif left_idx >= 0:
                    # Only point on left exists
                    visible_indices = np.array([left_idx])
                elif right_idx < len(all_time):
                    # Only point on right exists
                    visible_indices = np.array([right_idx])
                else:
                    # No data at all
                    continue
            else:
                # Expand to include edge points
                first_visible = visible_indices[0]
                last_visible = visible_indices[-1]

                # Include one point before first visible (if exists)
                if first_visible > 0:
                    visible_indices = np.concatenate([[first_visible - 1], visible_indices])

                # Include one point after last visible (if exists)
                if last_visible < len(all_time) - 1:
                    visible_indices = np.concatenate([visible_indices, [last_visible + 1]])

            visible_time = all_time[visible_indices]
            visible_values = all_values[visible_indices]

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

            # Get normalization range using DataNormalizer (Phase 4 refactoring)
            stream_min, stream_max = self.normalizer.calculate_stream_range(
                stream,
                self.raw_data,
                self.view_start,
                self.view_end,
                axis_owner_visible_range if stream == self.axis_owner else None
            )

            # Normalize the data using DataNormalizer (Phase 4 refactoring)
            normalize_max = getattr(self, 'dynamic_normalize_max',
                                   self.stream_config.get_setting('data_normalize_max', 0.85))
            bar_offset = getattr(self, 'bar_space_offset', 0.0)
            normalized_data = self.normalizer.normalize_data(
                plot_values,
                stream_min,
                stream_max,
                normalize_max,
                bar_offset
            )

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
        # Account for:
        # - Bar space at bottom (bar_space_offset)
        # - Data range (dynamic_normalize_max)
        # - Headroom above for graph markers (e.g., CRID with line_height=0.04)
        bar_offset = getattr(self, 'bar_space_offset', 0.0)
        normalize_max = getattr(self, 'dynamic_normalize_max',
                               self.stream_config.get_setting('data_normalize_max', 0.85))

        # Calculate top of display area: bar_offset + normalize_max + headroom for markers
        # Headroom of 0.10 provides space for CRID markers (line_height=0.04) plus text labels
        # Total: data at 0.85 + marker line 0.04 + text ~0.03 + margin 0.03 = ~0.10 headroom needed
        y_max = bar_offset + normalize_max + 0.10

        self.graph_plot.setXRange(self.view_start, self.view_end, padding=0)
        self.graph_plot.setYRange(-0.08, y_max, padding=0)

        # Set up custom tick formatter to show axis owner's real values with round numbers
        if self.axis_owner and self.axis_owner in self.enabled_streams:
            # Use visible range if available, otherwise fall back to full range
            if axis_owner_visible_range is not None:
                axis_min, axis_max = axis_owner_visible_range
            else:
                axis_min, axis_max = self.stream_ranges.get(self.axis_owner, (0, 1))

            axis_range = axis_max - axis_min
            if axis_range == 0:
                axis_range = 1  # Avoid division by zero for constant data

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

            # Apply minimum spacing constraints for specific stream types
            # Check if this is a temperature stream (contains "temp" in name)
            if 'temp' in self.axis_owner.lower():
                if tick_spacing_real < MIN_TEMPERATURE_TICK_SPACING_C:
                    tick_spacing_real = MIN_TEMPERATURE_TICK_SPACING_C
                    self.debug_print(f"  Applied MIN_TEMPERATURE_TICK_SPACING_C: {MIN_TEMPERATURE_TICK_SPACING_C}°C")

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
            self.debug_print(f"Stream: {self.axis_owner}")
            self.debug_print(f"  Data range: {axis_min:.1f} to {axis_max:.1f}")
            self.debug_print(f"  Rounded range: {axis_min_rounded:.1f} to {axis_max_rounded:.1f}")
            self.debug_print(f"  Tick spacing: {tick_spacing_real:.1f}")
            self.debug_print(f"  Real ticks: {real_ticks}")

            # Convert tick positions to DATA's normalized 0-1 space (where 0=axis_min, 1=axis_max)
            # This is where the ticks will actually be drawn since data is normalized to this range
            normalize_max = getattr(self, 'dynamic_normalize_max',
                                   self.stream_config.get_setting('data_normalize_max', 0.85))
            data_normalized_ticks = [((t - axis_min) / axis_range) * normalize_max for t in real_ticks]

            # If bars are enabled, shift tick positions up by bar offset to match shifted data
            bar_offset = getattr(self, 'bar_space_offset', 0.0)
            if bar_offset > 0:
                data_normalized_ticks = [pos + bar_offset for pos in data_normalized_ticks]

            self.debug_print(f"  Normalized tick positions: {data_normalized_ticks}")

            left_axis = self.graph_plot.getAxis('left')

            # Filter ticks to only those within the visible area (accounting for bar offset)
            min_visible = bar_offset
            max_visible = bar_offset + normalize_max
            visible_ticks = [(norm_pos, real_val) for norm_pos, real_val in zip(data_normalized_ticks, real_ticks)
                           if min_visible <= norm_pos <= max_visible]

            self.debug_print(f"  Visible ticks: {visible_ticks}")

            # Override tickValues to specify exact tick positions
            def custom_tick_values(minVal, maxVal, size):
                # Return list of [(spacing, [tick_positions])] for major and minor ticks
                # We return our pre-calculated positions
                major_ticks = [pos for pos, _ in visible_ticks]
                minor_ticks = []  # No minor ticks

                self.debug_print(f"  tickValues returning {len(major_ticks)} positions")
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
                self.debug_print(f"  tickStrings called with {len(values)} positions: {values[:5]}")
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
            # Get stream configuration for display range constraints
            right_stream_cfg = self.stream_config.get_stream(self.right_axis_owner)

            # Get visible data for right axis stream
            full_min, full_max = self.stream_ranges.get(self.right_axis_owner, (0, 1))

            # Check for fixed display range or constraints
            if right_stream_cfg and right_stream_cfg.display_range_min is not None and right_stream_cfg.display_range_max is not None:
                # Fixed range - no dynamic scaling
                axis_min = right_stream_cfg.display_range_min
                axis_max = right_stream_cfg.display_range_max
            else:
                # Dynamic scaling with optional constraints
                if self.right_axis_owner in self.raw_data:
                    stream_data = self.raw_data[self.right_axis_owner]
                    all_time = stream_data['time']
                    all_values = stream_data['values']
                    mask = (all_time >= self.view_start) & (all_time <= self.view_end)
                    visible_values = all_values[mask]

                    if len(visible_values) > 0:
                        visible_max = float(visible_values.max())
                        axis_min = full_min

                        # Check for minimum top constraint
                        if right_stream_cfg and right_stream_cfg.display_range_min_top is not None:
                            min_top = right_stream_cfg.display_range_min_top
                            if visible_max < min_top:
                                visible_max = min_top

                        axis_max = visible_max
                    else:
                        axis_min = full_min
                        axis_max = full_max
                else:
                    axis_min = full_min
                    axis_max = full_max

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

            # Apply minimum spacing constraints for specific stream types
            # Check if this is a temperature stream (contains "temp" in name)
            if 'temp' in self.right_axis_owner.lower():
                if tick_spacing_real < MIN_TEMPERATURE_TICK_SPACING_C:
                    tick_spacing_real = MIN_TEMPERATURE_TICK_SPACING_C
                    self.debug_print(f"  Applied MIN_TEMPERATURE_TICK_SPACING_C: {MIN_TEMPERATURE_TICK_SPACING_C}°C")

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
            self.debug_print(f"Right Axis Stream: {self.right_axis_owner}")
            self.debug_print(f"  Data range: {axis_min:.1f} to {axis_max:.1f}")
            self.debug_print(f"  Rounded range: {axis_min_rounded:.1f} to {axis_max_rounded:.1f}")
            self.debug_print(f"  Tick spacing: {tick_spacing_real:.1f}")
            self.debug_print(f"  Real ticks: {real_ticks}")

            # Convert tick positions to DATA's normalized 0-1 space (where 0=axis_min, 1=axis_max)
            # This is where the ticks will actually be drawn since data is normalized to this range
            data_normalized_ticks = [(t - axis_min) / axis_range for t in real_ticks]
            self.debug_print(f"  Normalized tick positions: {data_normalized_ticks}")

            right_axis = self.graph_plot.getAxis('right')

            # Filter ticks to only those within the 0-1 range (visible area)
            visible_ticks_right = [(norm_pos, real_val) for norm_pos, real_val in zip(data_normalized_ticks, real_ticks)
                           if 0 <= norm_pos <= 1]

            self.debug_print(f"  Visible ticks: {visible_ticks_right}")

            # Override tickValues to specify exact tick positions
            def custom_tick_values_right(minVal, maxVal, size):
                # Return list of [(spacing, [tick_positions])] for major and minor ticks
                # We return our pre-calculated positions
                major_ticks = [pos for pos, _ in visible_ticks_right]
                minor_ticks = []  # No minor ticks

                self.debug_print(f"  Right tickValues returning {len(major_ticks)} positions")
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
                self.debug_print(f"  Right tickStrings called with {len(values)} positions: {values[:5]}")
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

        # Draw event markers (spark, crankref, camshaft) and bars (injectors, coils)
        self.draw_spark_events()  # Now draws all graph markers from config
        self.draw_injector_bars()  # Draws all bar data from config

    def draw_spark_events(self):
        """Draw all graph marker events (spark, crankref, camshaft) from config"""
        # Get all graph marker streams from config
        marker_streams = self.stream_config.get_streams_by_type(StreamType.GRAPH_MARKER)

        for stream_name, stream_cfg in marker_streams.items():
            # Only draw if enabled
            if stream_name not in self.enabled_streams:
                continue

            # Draw this marker stream
            self._draw_graph_marker_stream(stream_name, stream_cfg)

    def _draw_graph_marker_stream(self, stream_name, config):
        """
        Generic function to draw graph marker events positioned relative to their attach_to stream.

        Args:
            stream_name: Name of the marker stream (e.g., 'ecu_spark_x1')
            config: StreamConfig object with display properties
        """
        # Check if we have the attach_to stream (usually RPM)
        attach_to = config.attach_to
        if not attach_to or attach_to not in self.raw_data:
            self.debug_print(f"DEBUG: {stream_name} - attach_to stream '{attach_to}' not found in raw_data")
            return

        if stream_name not in self.raw_data:
            self.debug_print(f"DEBUG: {stream_name} - marker stream not found in raw_data")
            return

        # Get the base stream data (e.g., RPM)
        base_data = self.raw_data[attach_to]
        base_time = base_data['time']
        base_values = base_data['values']

        # Get normalization range: full dataset min, visible max
        full_base_min, full_base_max = self.stream_ranges.get(attach_to, (0, 1))

        # Calculate visible max for the base stream
        visible_base_mask = (base_time >= self.view_start) & (base_time <= self.view_end)
        visible_base_values = base_values[visible_base_mask]
        if len(visible_base_values) > 0:
            visible_base_max = float(visible_base_values.max())
            base_min = full_base_min
            base_max = visible_base_max
        else:
            base_min = full_base_min
            base_max = full_base_max

        # Avoid division by zero
        if base_max - base_min < 1e-10:
            base_max = base_min + 1.0

        # Get marker event data
        marker_data = self.raw_data[stream_name]
        marker_times = marker_data['time']
        marker_values = marker_data.get('values', None)  # May have values (e.g., CRID) or not (e.g., spark)

        # DEBUG: Print marker time range
        if len(marker_times) > 0:
            self.debug_print(f"DEBUG: {stream_name} - total markers: {len(marker_times)}, time range: {marker_times[0]:.2f} - {marker_times[-1]:.2f}")
            self.debug_print(f"DEBUG: {stream_name} - attach_to '{attach_to}' time range: {base_time[0]:.2f} - {base_time[-1]:.2f}")
            self.debug_print(f"DEBUG: {stream_name} - current view window: {self.view_start:.2f} - {self.view_end:.2f}")

            # Check for gaps in spark data around the problematic time
            if self.view_start < 21 and self.view_end > 18:
                # Find markers in the 18-21 second range
                early_mask = (marker_times >= 18) & (marker_times <= 21)
                early_markers = marker_times[early_mask]
                if len(early_markers) > 0:
                    self.debug_print(f"DEBUG: {stream_name} - markers in 18-21s range: {len(early_markers)}")
                    self.debug_print(f"DEBUG: {stream_name} - first few times: {early_markers[:10]}")
                    self.debug_print(f"DEBUG: {stream_name} - last few times: {early_markers[-10:]}")

        # Get visual properties from config
        color = self.stream_colors.get(stream_name, config.default_color)
        offset = config.offset
        line_height = config.line_height
        label = config.label
        label_format = config.label_format
        max_visible = config.max_visible or self.stream_config.get_setting('performance.max_event_markers_visible', MAX_VISIBLE_EVENT_MARKERS)

        # Filter to visible time window
        mask = (marker_times >= self.view_start) & (marker_times <= self.view_end)
        visible_marker_times = marker_times[mask]
        if marker_values is not None:
            visible_marker_values = marker_values[mask]
        else:
            visible_marker_values = None

        self.debug_print(f"DEBUG: {stream_name} - visible markers in window: {len(visible_marker_times)}")

        # Skip drawing if too many markers
        if len(visible_marker_times) > max_visible:
            self.debug_print(f"DEBUG: Skipping {stream_name} markers - {len(visible_marker_times)} exceeds limit of {max_visible}")
            return

        # Get normalization factor - use dynamic max if bars are enabled
        normalize_max = getattr(self, 'dynamic_normalize_max',
                               self.stream_config.get_setting('data_normalize_max', 0.85))

        # Get bar offset if bars are enabled
        bar_offset = getattr(self, 'bar_space_offset', 0.0)

        # Draw each marker
        for i, marker_time in enumerate(visible_marker_times):
            # Interpolate base value at marker time, or use midpoint if outside attach_to range
            if marker_time < base_time[0] or marker_time > base_time[-1]:
                # Marker occurs before/after attach_to stream data
                # Use midpoint of graph height for positioning
                normalized_base = normalize_max / 2.0
                if bar_offset > 0:
                    normalized_base = normalized_base + bar_offset
            else:
                base_at_marker = np.interp(marker_time, base_time, base_values)
                normalized_base = ((base_at_marker - base_min) / (base_max - base_min)) * normalize_max

                # Shift up by bar offset if bars are enabled
                if bar_offset > 0:
                    normalized_base = normalized_base + bar_offset

            # Calculate label position
            if line_height != 0:
                # Vertical line with specified height (upward or downward)
                # line_height determines the line length, offset is ignored
                if line_height > 0:
                    # Upward line
                    label_y = normalized_base + line_height
                    line_start_y = normalized_base
                    line_end_y = label_y
                    text_anchor = (0.5, 1.0)  # Bottom center
                else:
                    # Downward line (negative line_height)
                    label_y = normalized_base + line_height
                    line_start_y = normalized_base
                    line_end_y = label_y
                    text_anchor = (0.5, 0.0)  # Top center
                draw_line = True
            elif offset != 0:
                # Offset-based positioning (spark events style)
                # Draw line from base to label position
                label_y = normalized_base + offset
                line_start_y = normalized_base
                line_end_y = label_y
                text_anchor = (0.5, 0.5)  # Center
                draw_line = True
            else:
                # No line, label at base position
                label_y = normalized_base
                line_start_y = normalized_base
                line_end_y = label_y
                text_anchor = (0.5, 0.5)  # Center
                draw_line = False

            # Draw line if needed
            if draw_line:
                line_item = pg.PlotDataItem(
                    [marker_time, marker_time],
                    [line_start_y, line_end_y],
                    pen=pg.mkPen(color=color, width=2.0, style=Qt.PenStyle.SolidLine)
                )
                self.graph_plot.addItem(line_item)

            # Format label text
            if label_format and visible_marker_values is not None:
                # Use format string with value
                label_text = label_format.format(value=int(visible_marker_values[i]))
            elif label:
                # Use fixed label
                label_text = label
            else:
                # No label
                continue

            # Draw text
            text_item = pg.TextItem(text=label_text, color=color, anchor=text_anchor)
            text_item.setPos(marker_time, label_y)
            self.graph_plot.addItem(text_item)

    def draw_injector_bars(self):
        """Draw all bar streams (injectors, coils) on the graph."""

        # Get all bar_data streams from config
        bar_streams = self.stream_config.get_streams_by_type(StreamType.BAR_DATA)

        # Calculate dynamic Y positions for enabled bars
        enabled_bars = [(name, cfg) for name, cfg in bar_streams.items()
                       if name in self.enabled_streams and name in self.raw_data]

        if len(enabled_bars) > 0:
            # Assign Y positions dynamically - bars stack below graph data
            bar_y_positions = self._calculate_bar_positions(enabled_bars)
        else:
            bar_y_positions = {}

        # Draw each enabled bar
        for stream_name, stream_cfg in enabled_bars:
            # Get dynamically calculated Y position
            bar_y = bar_y_positions.get(stream_name, stream_cfg.y_position)

            # Draw the bar with its assigned position
            self._draw_combined_bars(stream_name, stream_cfg, bar_y)

    def _draw_combined_bars(self, stream_name, config, bar_y):
        """
        Draw bars using combined format [time_ns, duration].

        Args:
            stream_name: Name of the combined bar stream (e.g., 'ecu_front_inj')
            config: StreamConfig object with display properties
            bar_y: Y position for this bar (dynamically calculated)
        """
        # Check data availability
        if stream_name not in self.raw_data:
            return

        # Get combined data
        stream_data = self.raw_data[stream_name]
        bar_times = stream_data['time']
        duration_values = stream_data['values']  # Duration in ticks or nanoseconds

        if len(bar_times) == 0:
            return

        # Get visual properties from config
        color = self.stream_colors.get(stream_name, config.default_color)
        bar_height = config.height
        bar_alpha = config.alpha

        # Convert duration based on units (needed for visibility filtering)
        duration_units = config.duration_units or "ticks"  # Default to ticks for backward compat

        # Convert all durations to seconds for end-time calculation
        if duration_units == "ticks":
            # Timer ticks: 2 microseconds per tick
            duration_seconds = duration_values * TIMER_TICK_DURATION_S
            # Validate reasonable duration (0 to 50ms)
            valid_mask = (duration_values > 0) & (duration_values <= MAX_REASONABLE_DURATION_TICKS)
        elif duration_units == "nanoseconds":
            # Nanoseconds: convert directly
            duration_seconds = duration_values / 1e9
            # Validate reasonable duration (0 to 100ms)
            valid_mask = (duration_values > 0) & (duration_values <= 100_000_000)
        else:
            # Unknown units, skip all
            return

        # Calculate end times for all bars
        end_times = bar_times + duration_seconds

        # Filter to visible window: include bars where ANY part is visible
        # - Bar starts before view_end (bar_times <= view_end), AND
        # - Bar ends after view_start (end_times >= view_start), AND
        # - Duration is valid
        mask = (bar_times <= self.view_end) & (end_times >= self.view_start) & valid_mask
        visible_bar_times = bar_times[mask]
        visible_durations = duration_seconds[mask]

        if len(visible_bar_times) == 0:
            return

        # Performance limit
        max_bars = self.stream_config.get_setting('performance.max_injector_bars_visible', MAX_INJECTOR_BARS_VISIBLE)
        if len(visible_bar_times) > max_bars:
            step = len(visible_bar_times) // max_bars
            visible_bar_times = visible_bar_times[::step]
            visible_durations = visible_durations[::step]

        # Draw bars
        rgba = parse_color_to_rgba(color, bar_alpha)

        for i in range(len(visible_bar_times)):
            start_time = visible_bar_times[i]
            duration_seconds = visible_durations[i]
            duration_us = duration_seconds * 1e6  # Convert to microseconds

            # Add tooltip - use template from config if available
            if config.tooltip:
                # Use template from YAML config and format it
                tooltip_text = config.tooltip.format(
                    start_time=start_time,
                    duration_us=duration_us
                )
            else:
                # Fallback to default format (duration only)
                tooltip_text = f"{config.display_name}\nDuration: {duration_us:.1f} μs"

            # Create custom bar item with proper hover detection
            rect = InjectorBarItem(
                start_time, bar_y, duration_seconds, bar_height,
                tooltip_text, color, bar_alpha
            )

            self.graph_plot.addItem(rect)

    def _calculate_bar_positions(self, enabled_bars):
        """
        Calculate dynamic Y positions for bars to stack compactly below graph data.

        Bars are positioned at the BOTTOM of the graph area (low Y values, near 0).
        Graph data is shifted UP to start ABOVE the bars.

        Args:
            enabled_bars: List of (stream_name, config) tuples for enabled bars

        Returns:
            Dictionary mapping stream_name -> y_position
        """
        positions = {}

        # Calculate how much vertical space the bars need
        bar_spacing = 0.01  # Small gap between bars
        total_bar_space = sum(cfg.height + bar_spacing for _, cfg in enabled_bars)

        # Get the default data normalization max (how high graph data goes without bars)
        default_normalize_max = self.stream_config.get_setting('data_normalize_max', 0.85)

        # When bars are enabled, shift the graph data UP by adding total_bar_space as offset
        # Graph data will use: [total_bar_space] to [total_bar_space + default_normalize_max]
        # Bars will use: [0] to [total_bar_space]

        # Store the base offset - data normalization will ADD this to normalized values
        self.bar_space_offset = total_bar_space
        # Keep normalize_max the same, but data will be shifted up
        self.dynamic_normalize_max = default_normalize_max

        # Assign Y positions starting at bottom (Y=0)
        current_y = 0.0
        for stream_name, cfg in enabled_bars:
            positions[stream_name] = current_y
            current_y += cfg.height + bar_spacing

        return positions

    def update_navigation_plot(self):
        """Update the navigation plot with decimated overview"""
        self.nav_plot.clear()
        self.nav_plot.addItem(self.view_region)

        if not self.raw_data or len(self.enabled_streams) == 0:
            return

        # Use actual time range from data (time_min to time_max)
        time_min = getattr(self, 'time_min', 0)
        time_max = getattr(self, 'time_max', self.total_time_span)
        self.nav_plot.setXRange(time_min, time_max, padding=0)
        # Set Y range to 0-1 since all streams are normalized
        self.nav_plot.setYRange(0, 1, padding=0)

        # Update view region to match current view
        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)

        # Plot enabled streams with heavy decimation for overview
        max_nav_points = 1000
        for stream in self.enabled_streams:
            if stream not in self.raw_data:
                continue

            # Skip event-only streams (markers, bars, etc.) - not plotted in navigation view
            if self.stream_config.is_event_only_stream(stream):
                continue

            # Skip duration/helper streams that shouldn't be plotted
            if self.stream_config.should_skip_in_selection(stream):
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

            # Normalize to 0-1 range like main graph, using display constraints
            # Use the entire time range for navigation (not just visible window)
            if self.normalizer:
                stream_min, stream_max = self.normalizer.calculate_stream_range(
                    stream,
                    self.raw_data,
                    time_min,  # Use full data range for navigation
                    time_max,
                    axis_owner_range=None
                )
            else:
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

        # Save the starting position on first change (for undo)
        if not hasattr(self, '_region_drag_start'):
            self._region_drag_start = (self.view_start, self.view_end)

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

        # Sync to view controller so keyboard shortcuts use current position
        self.view_controller.set_view_range(new_start, new_end)

        self.request_update()

    def on_region_change_finished(self):
        """Handle navigation region change completion"""
        # Add the NEW position to history (after the drag)
        self.add_to_history(self.view_start, self.view_end, 0, 1)

        # Clean up drag tracking
        if hasattr(self, '_region_drag_start'):
            delattr(self, '_region_drag_start')

    def on_nav_zoom(self, x_min, x_max, y_min, y_max):
        """Handle rubber band zoom in navigation plot - zoom to selected time range"""
        # Only zoom on X-axis (time), ignore Y since navigation always shows 0-1
        self.view_start = max(0, x_min)
        self.view_end = min(self.total_time_span, x_max)

        # Ensure minimum duration
        if self.view_end - self.view_start < 0.1:
            mid = (self.view_start + self.view_end) / 2
            self.view_start = max(0, mid - 0.05)
            self.view_end = min(self.total_time_span, mid + 0.05)

        # Add the NEW view to history (after modifying)
        self.add_to_history(self.view_start, self.view_end, 0, 1)

        # Sync to view controller so keyboard shortcuts use current position
        self.view_controller.set_view_range(self.view_start, self.view_end)

        # Update the navigation region to show the new view
        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)

        # Update main graph
        self.update_graph_plot()

    def handle_graph_zoom(self, x_min, x_max, y_min, y_max):
        """Handle rubber band zoom in graph - only X-axis zoom, Y is always 0-1"""
        # Only zoom on X-axis (time), ignore Y since streams are independently scaled
        self.view_start = max(0, x_min)
        self.view_end = min(self.total_time_span, x_max)

        # Add NEW view to history (after modifying)
        self.add_to_history(self.view_start, self.view_end, 0, 1)

        # Sync to view controller so keyboard shortcuts use current position
        self.view_controller.set_view_range(self.view_start, self.view_end)

        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)

        self.update_graph_plot()
    

    def zoom_in_2x(self):
        """Zoom in by 2x (halve the time shown), centered on current view"""
        # Use view controller to perform zoom
        self.view_controller.zoom_in_2x()

        # Update legacy accessors
        self.view_start, self.view_end, _, _ = self.view_controller.get_view_range()

        # Add NEW view to history (after modifying)
        self.view_history.push(self.view_start, self.view_end, 0, 1)

        # Update navigation region
        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)

        self.update_graph_plot()

    def zoom_out_2x(self):
        """Zoom out by 2x (double the time shown), centered on current view"""
        # Use view controller to perform zoom
        self.view_controller.zoom_out_2x()

        # Update legacy accessors
        self.view_start, self.view_end, _, _ = self.view_controller.get_view_range()

        # Add NEW view to history (after modifying)
        self.view_history.push(self.view_start, self.view_end, 0, 1)

        # Update navigation region
        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)

        self.update_graph_plot()

    def pan_left_50(self):
        """Pan left by 50% of current view duration"""
        self.view_controller.pan_left(0.5)
        self.view_start, self.view_end, _, _ = self.view_controller.get_view_range()
        self.view_history.push(self.view_start, self.view_end, 0, 1)
        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)
        self.update_graph_plot()

    def pan_right_50(self):
        """Pan right by 50% of current view duration"""
        self.view_controller.pan_right(0.5)
        self.view_start, self.view_end, _, _ = self.view_controller.get_view_range()
        self.view_history.push(self.view_start, self.view_end, 0, 1)
        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)
        self.update_graph_plot()

    def pan_left_15(self):
        """Pan left by 15% of current view duration"""
        self.view_controller.pan_left(0.15)
        self.view_start, self.view_end, _, _ = self.view_controller.get_view_range()
        self.view_history.push(self.view_start, self.view_end, 0, 1)
        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)
        self.update_graph_plot()

    def pan_right_15(self):
        """Pan right by 15% of current view duration"""
        self.view_controller.pan_right(0.15)
        self.view_start, self.view_end, _, _ = self.view_controller.get_view_range()
        self.view_history.push(self.view_start, self.view_end, 0, 1)
        self.view_region.blockSignals(True)
        self.view_region.setRegion([self.view_start, self.view_end])
        self.view_region.blockSignals(False)
        self.update_graph_plot()

    def add_to_history(self, start, end, y_min, y_max):
        """Add state to history - delegated to view_history"""
        self.view_history.push(start, end, y_min, y_max)

    def undo(self):
        """Undo last zoom or pan operation"""
        result = self.view_history.undo()
        if result:
            start, end, _, _ = result
            self.view_start = start
            self.view_end = end

            # Sync to view controller so keyboard shortcuts use correct position
            self.view_controller.set_view_range(start, end)

            # Update navigation region
            self.view_region.blockSignals(True)
            self.view_region.setRegion([start, end])
            self.view_region.blockSignals(False)

            # Update main graph
            self.update_graph_plot()

    def redo(self):
        """Redo previously undone zoom or pan operation"""
        result = self.view_history.redo()
        if result:
            start, end, _, _ = result
            self.view_start = start
            self.view_end = end

            # Sync to view controller so keyboard shortcuts use correct position
            self.view_controller.set_view_range(start, end)

            # Update navigation region
            self.view_region.blockSignals(True)
            self.view_region.setRegion([start, end])
            self.view_region.blockSignals(False)

            # Update main graph
            self.update_graph_plot()

    def update_history_buttons(self):
        """Update undo/redo action states - now handled by view_history signal"""
        pass  # Kept for backward compatibility but functionality moved to _on_history_changed

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
            self.debug_print("No GPS data available")
            return

        gps_data = self.raw_data['gps_position']
        if len(gps_data['time']) == 0:
            self.debug_print("No GPS data available")
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
            self.debug_print("No GPS data available")
            return

        gps_data = self.raw_data['gps_position']

        # Get the clicked point info
        clicked_index = int(points[0].data())
        clicked_time = gps_data['time'][clicked_index]
        clicked_lat = gps_data['lat'][clicked_index]
        clicked_lon = gps_data['lon'][clicked_index]

        self.debug_print(f"GPS marker clicked at time={clicked_time:.3f}s, lat={clicked_lat:.6f}, lon={clicked_lon:.6f}")
        self.debug_print(f"Generating map with all {len(gps_data['time'])} GPS positions...")

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
                self.debug_print(f"Opening GPS track map at: {windows_path}")
                subprocess.Popen(['powershell.exe', '-Command', 'Start-Process', f'"{windows_path}"'])
            else:
                import webbrowser
                file_url = f'file:///{html_file.replace(os.sep, "/")}'
                webbrowser.open(file_url, new=2)
        except Exception as e:
            self.debug_print(f"Error opening browser: {e}")
            self.debug_print(f"Map file: {html_file}")

    def _save_per_file_settings(self):
        """Save current visualizer state to .viz file."""
        if not self.current_file:
            return False

        # Gather current state
        app_state = {
            'enabled_streams': self.enabled_streams.copy(),
            'stream_colors': self.stream_colors.copy(),
            'stream_display_modes': {},
            'stream_order': [],
            'axis_owner': self.axis_owner,
            'right_axis_owner': self.right_axis_owner,
            'view_start': self.view_start,
            'view_end': self.view_end,
            'view_history': self.view_history
        }

        # Get display modes from stream widgets
        for widget in self.stream_list_widget.stream_widgets:
            app_state['stream_display_modes'][widget.stream_name] = widget.display_mode

        # Get stream order from widget order
        for widget in self.stream_list_widget.stream_widgets:
            app_state['stream_order'].append(widget.stream_name)

        # Capture and save
        settings = self.per_file_settings_manager.capture_current_state(app_state)
        success = self.per_file_settings_manager.save(self.current_file, settings)

        if success:
            self.debug_print(f"Saved per-file settings to .viz file")
        return success

    def save_layout_manual(self):
        """Manually save layout (Ctrl+S handler)."""
        if self._save_per_file_settings():
            QMessageBox.information(
                self,
                "Layout Saved",
                f"Visualizer layout saved to:\n{self.per_file_settings_manager.current_viz_path}",
                QMessageBox.StandardButton.Ok
            )
        else:
            QMessageBox.warning(
                self,
                "Save Failed",
                "No file is currently loaded.",
                QMessageBox.StandardButton.Ok
            )

    def reset_layout(self):
        """Reset layout by deleting .viz file and reloading."""
        if not self.current_file:
            QMessageBox.warning(
                self,
                "No File Loaded",
                "No file is currently loaded.",
                QMessageBox.StandardButton.Ok
            )
            return

        # Confirm with user
        viz_path = self.per_file_settings_manager.get_viz_path(self.current_file)
        reply = QMessageBox.question(
            self,
            "Reset Layout",
            f"This will delete the saved layout and reload the file with default settings.\n\n"
            f"Layout file: {viz_path}\n\n"
            f"Continue?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )

        if reply == QMessageBox.StandardButton.Yes:
            # Delete .viz file if it exists
            if os.path.exists(viz_path):
                try:
                    os.remove(viz_path)
                    self.debug_print(f"Deleted .viz file: {viz_path}")
                except Exception as e:
                    QMessageBox.critical(
                        self,
                        "Error",
                        f"Failed to delete layout file:\n{e}",
                        QMessageBox.StandardButton.Ok
                    )
                    return

            # Reload file
            current_file = self.current_file
            self.load_hdf5_file_internal(current_file)

    def import_layout(self):
        """Import layout from another .viz file."""
        if not self.current_file:
            QMessageBox.warning(
                self,
                "No File Loaded",
                "No file is currently loaded.",
                QMessageBox.StandardButton.Ok
            )
            return

        # File picker to select source .viz file
        source_viz, _ = QFileDialog.getOpenFileName(
            self,
            "Select Layout File to Import",
            os.path.dirname(self.current_file),
            "Layout Files (*.viz);;All Files (*)"
        )

        if not source_viz:
            return  # User cancelled

        # Load source .viz file
        try:
            with open(source_viz, 'r') as f:
                import json
                source_data = json.load(f)

            # Create settings object
            source_settings = PerFileSettings(
                version=source_data.get('version', 1),
                logfile=source_data.get('logfile', ''),
                enabled_streams=source_data.get('enabled_streams', []),
                stream_colors=source_data.get('stream_colors', {}),
                stream_display_modes=source_data.get('stream_display_modes', {}),
                stream_order=source_data.get('stream_order', []),
                axis_owner=source_data.get('axis_owner'),
                right_axis_owner=source_data.get('right_axis_owner'),
                view_start=source_data.get('view_start'),
                view_end=source_data.get('view_end'),
                view_history=source_data.get('view_history')
            )

            # Validate against current file
            source_settings = self.per_file_settings_manager.validate_against_file(
                source_settings,
                self.data_streams
            )
            source_settings = self.per_file_settings_manager.validate_view_bounds(
                source_settings,
                self.time_min,
                self.time_max
            )

            # Save to current file's .viz path
            if self.per_file_settings_manager.save(self.current_file, source_settings):
                # Reload current file to apply imported settings
                current_file = self.current_file
                self.load_hdf5_file_internal(current_file)

                QMessageBox.information(
                    self,
                    "Layout Imported",
                    f"Layout imported from:\n{os.path.basename(source_viz)}\n\n"
                    f"File reloaded with imported settings.",
                    QMessageBox.StandardButton.Ok
                )
            else:
                QMessageBox.critical(
                    self,
                    "Import Failed",
                    "Failed to save imported layout.",
                    QMessageBox.StandardButton.Ok
                )

        except json.JSONDecodeError as e:
            QMessageBox.critical(
                self,
                "Invalid File",
                f"Selected file is not a valid layout file:\n{e}",
                QMessageBox.StandardButton.Ok
            )
        except Exception as e:
            QMessageBox.critical(
                self,
                "Import Error",
                f"Error importing layout:\n{e}",
                QMessageBox.StandardButton.Ok
            )

    def closeEvent(self, event):
        """Handle window close event - save configuration."""
        # Auto-save per-file settings
        self._save_per_file_settings()

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
  %(prog)s logfile.h5         # Open and load HDF5 log file
  %(prog)s log_123.um4        # Open binary log (offers conversion to HDF5)
  %(prog)s /path/to/data.h5   # Open with absolute path
        """
    )
    parser.add_argument(
        'logfile',
        nargs='?',
        default=None,
        help='Log file to open (.h5, .hdf5, or .um4)'
    )
    parser.add_argument(
        '--debug',
        action='store_true',
        help='Enable debug output'
    )
    args = parser.parse_args()

    app = QApplication(sys.argv)
    app.setStyle('Fusion')

    # Set organization and application name for QSettings
    QApplication.setOrganizationName("umod4")
    QApplication.setApplicationName("LogVisualizer")

    window = DataVisualizationTool(debug=args.debug)
    window.show()

    # Raise window to front to ensure it's visible
    window.raise_()
    window.activateWindow()

    # Load file if specified on command line
    if args.logfile:
        if os.path.exists(args.logfile):
            window.load_file_with_conversion(args.logfile)
        else:
            if args.debug:
                print(f"Error: File not found: {args.logfile}")

    sys.exit(app.exec())

if __name__ == '__main__':
    main()
