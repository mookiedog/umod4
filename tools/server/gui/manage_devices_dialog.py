"""Device management dialog."""

from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QPushButton,
                               QTableWidget, QTableWidgetItem, QHeaderView,
                               QMessageBox, QInputDialog, QFileDialog, QLabel)
from PySide6.QtCore import Qt
import os


class ManageDevicesDialog(QDialog):
    """Dialog for managing all known devices."""

    def __init__(self, database, parent=None):
        super().__init__(parent)
        self.database = database
        self.setWindowTitle("Manage Devices")
        self.setModal(True)
        self.resize(700, 400)
        self._setup_ui()
        self.refresh_devices()

    def _setup_ui(self):
        """Set up the dialog UI."""
        layout = QVBoxLayout(self)

        # Header
        header = QLabel("All Known Devices")
        header.setStyleSheet("font-weight: bold; font-size: 14px;")
        layout.addWidget(header)

        # Device table
        self.device_table = QTableWidget()
        self.device_table.setColumnCount(4)
        self.device_table.setHorizontalHeaderLabels([
            "Display Name", "MAC Address", "WP Version", "Log Storage Path"
        ])
        self.device_table.horizontalHeader().setStretchLastSection(True)
        self.device_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.device_table.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
        layout.addWidget(self.device_table)

        # Buttons
        button_layout = QHBoxLayout()

        self.rename_button = QPushButton("Rename Device")
        self.rename_button.clicked.connect(self._rename_device)
        button_layout.addWidget(self.rename_button)

        self.change_path_button = QPushButton("Change Log Path")
        self.change_path_button.clicked.connect(self._change_log_path)
        button_layout.addWidget(self.change_path_button)

        self.delete_button = QPushButton("Delete Device")
        self.delete_button.clicked.connect(self._delete_device)
        button_layout.addWidget(self.delete_button)

        button_layout.addStretch()

        self.close_button = QPushButton("Close")
        self.close_button.clicked.connect(self.accept)
        button_layout.addWidget(self.close_button)

        layout.addLayout(button_layout)

    def refresh_devices(self):
        """Refresh the device list from database."""
        from models.database import Device

        session = self.database.get_session()
        try:
            devices = session.query(Device).order_by(Device.display_name).all()

            self.device_table.setRowCount(len(devices))

            for row, device in enumerate(devices):
                # Display name
                name_item = QTableWidgetItem(device.display_name)
                name_item.setData(Qt.ItemDataRole.UserRole, device.mac_address)
                self.device_table.setItem(row, 0, name_item)

                # MAC address
                self.device_table.setItem(row, 1, QTableWidgetItem(device.mac_address))

                # WP version
                wp_version = device.wp_version or "-"
                self.device_table.setItem(row, 2, QTableWidgetItem(wp_version))

                # Log storage path
                self.device_table.setItem(row, 3, QTableWidgetItem(device.log_storage_path))

        finally:
            session.close()

    def _get_selected_mac(self):
        """Get MAC address of selected device.

        Returns:
            str or None: MAC address if device selected, None otherwise
        """
        selected_rows = self.device_table.selectedItems()
        if not selected_rows:
            QMessageBox.warning(self, "No Selection", "Please select a device first.")
            return None

        row = selected_rows[0].row()
        mac_item = self.device_table.item(row, 0)
        return mac_item.data(Qt.ItemDataRole.UserRole)

    def _rename_device(self):
        """Rename selected device."""
        from models.database import Device

        mac_address = self._get_selected_mac()
        if not mac_address:
            return

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=mac_address).first()
            if not device:
                return

            new_name, ok = QInputDialog.getText(
                self,
                "Rename Device",
                f"Enter new name for {mac_address}:",
                text=device.display_name
            )

            if ok and new_name.strip():
                device.display_name = new_name.strip()
                session.commit()
                self.refresh_devices()

        finally:
            session.close()

    def _change_log_path(self):
        """Change log storage path for selected device."""
        from models.database import Device

        mac_address = self._get_selected_mac()
        if not mac_address:
            return

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=mac_address).first()
            if not device:
                return

            new_path = QFileDialog.getExistingDirectory(
                self,
                "Select Log Storage Directory",
                device.log_storage_path
            )

            if new_path:
                device.log_storage_path = new_path
                session.commit()
                os.makedirs(new_path, exist_ok=True)
                self.refresh_devices()

        finally:
            session.close()

    def _delete_device(self):
        """Delete selected device from database."""
        from models.database import Device

        mac_address = self._get_selected_mac()
        if not mac_address:
            return

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=mac_address).first()
            if not device:
                return

            # Confirm deletion
            reply = QMessageBox.question(
                self,
                "Delete Device",
                f"Delete device '{device.display_name}' ({mac_address})?\n\n"
                f"The device will be re-discovered and auto-configured with defaults "
                f"the next time it connects.\n\n"
                f"Log files in {device.log_storage_path} will NOT be deleted.",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No
            )

            if reply == QMessageBox.StandardButton.Yes:
                session.delete(device)
                session.commit()
                self.refresh_devices()

                QMessageBox.information(
                    self,
                    "Device Deleted",
                    f"Device {mac_address} has been deleted.\n\n"
                    f"It will be re-discovered with default settings when it next connects."
                )

        finally:
            session.close()
