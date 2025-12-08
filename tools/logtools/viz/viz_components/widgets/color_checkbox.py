"""
Custom checkbox widget that fills with color when checked.
"""

from PyQt6.QtWidgets import QCheckBox
from PyQt6.QtGui import QPainter, QColor


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
            painter = QPainter(self)
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)

            # Fill the checkbox with the stream color
            painter.fillRect(2, 2, 12, 12, QColor(self.fill_color))
            painter.end()
