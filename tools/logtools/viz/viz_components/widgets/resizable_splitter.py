"""
Custom splitter widget with enhanced styling.
"""

from PySide6.QtWidgets import QSplitter


class ResizableSplitter(QSplitter):
    """Custom splitter that can hide/show panes with minimum size handling"""
    def __init__(self, orientation, parent=None):
        super().__init__(orientation, parent)
        self.setHandleWidth(3)
        self.setStyleSheet("QSplitter::handle { background-color: black; }")
        self.min_size = 40
