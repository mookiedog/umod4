"""
Custom list widget that supports drag-and-drop reordering with visual feedback.
"""

from PySide6.QtWidgets import QWidget, QVBoxLayout
from PySide6.QtGui import QPainter, QPen, QColor

from .stream_checkbox import StreamCheckbox


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
