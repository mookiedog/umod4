"""
Custom stream selection widget with checkbox, label, and drag-drop support.
"""

from PySide6.QtWidgets import (QWidget, QHBoxLayout, QLabel, QApplication,
                             QColorDialog, QMenu)
from PySide6.QtCore import Qt, QMimeData
from PySide6.QtGui import QFont, QColor, QAction, QDrag

from .color_checkbox import ColorCheckbox


class StreamCheckbox(QWidget):
    """Custom widget for stream selection with color-filled checkbox and colored label - supports drag and drop"""
    def __init__(self, stream_name, color, display_name=None, parent=None):
        super().__init__(parent)
        self.stream_name = stream_name
        self.display_name = display_name or stream_name  # Use display name if provided
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

        # Create a drag handle area (the label area) - use display name
        self.label = QLabel(self.display_name)
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
