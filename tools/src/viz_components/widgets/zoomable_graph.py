"""
Graph widget with rubber band zoom capability.
"""

import pyqtgraph as pg
from PyQt6.QtCore import Qt, QPointF
from pyqtgraph import QtWidgets


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
