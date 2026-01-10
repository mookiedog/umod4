"""Device configuration dialog for new devices."""

from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel,
                               QPushButton, QLineEdit, QFileDialog, QFormLayout,
                               QDialogButtonBox)
from PySide6.QtCore import Qt
import os


class DeviceConfigDialog(QDialog):
    """Dialog for configuring a newly detected device."""

    def __init__(self, device_mac, default_name, default_path, parent=None):
        super().__init__(parent)
        self.device_mac = device_mac
        self.device_name = default_name
        self.log_path = default_path

        self.setWindowTitle("New Device Detected")
        self.setModal(True)
        self.resize(500, 200)

        self._setup_ui()

    def _setup_ui(self):
        """Set up the dialog UI."""
        layout = QVBoxLayout(self)

        # Header
        header = QLabel(f"<b>New umod4 device detected!</b>")
        header.setStyleSheet("font-size: 14px;")
        layout.addWidget(header)

        mac_label = QLabel(f"MAC Address: {self.device_mac}")
        mac_label.setStyleSheet("color: #666;")
        layout.addWidget(mac_label)

        layout.addSpacing(10)

        # Form
        form_layout = QFormLayout()

        # Device name
        self.name_edit = QLineEdit(self.device_name)
        self.name_edit.setPlaceholderText("e.g., Tuono Red, Test Bench")
        form_layout.addRow("Device Name:", self.name_edit)

        # Log storage path
        path_layout = QHBoxLayout()
        self.path_edit = QLineEdit(self.log_path)
        self.path_edit.setReadOnly(True)
        path_layout.addWidget(self.path_edit)

        browse_button = QPushButton("Browse...")
        browse_button.clicked.connect(self._browse_path)
        path_layout.addWidget(browse_button)

        form_layout.addRow("Log Storage Path:", path_layout)

        layout.addLayout(form_layout)

        # Info label
        info_label = QLabel(
            "Log files from this device will be saved to the specified directory.\n"
            "You can change these settings later by right-clicking the device."
        )
        info_label.setWordWrap(True)
        info_label.setStyleSheet("color: #666; font-size: 11px;")
        layout.addWidget(info_label)

        layout.addSpacing(10)

        # Buttons
        button_box = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        button_box.accepted.connect(self._accept)
        button_box.rejected.connect(self.reject)
        layout.addWidget(button_box)

    def _browse_path(self):
        """Browse for log storage directory."""
        path = QFileDialog.getExistingDirectory(
            self,
            "Select Log Storage Directory",
            self.path_edit.text()
        )
        if path:
            self.path_edit.setText(path)
            self.log_path = path

    def _accept(self):
        """Accept dialog and save values."""
        self.device_name = self.name_edit.text().strip()
        if not self.device_name:
            self.device_name = self.device_mac  # Default to MAC if empty

        self.log_path = self.path_edit.text()
        self.accept()

    def get_config(self):
        """Get the configured device name and path.

        Returns:
            tuple: (device_name, log_path)
        """
        return (self.device_name, self.log_path)
