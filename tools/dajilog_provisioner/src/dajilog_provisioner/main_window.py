from __future__ import annotations

from datetime import datetime

from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QComboBox,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)
from serial.tools import list_ports

from .usb_cdc import UsbCdcWorker


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.worker: UsbCdcWorker | None = None
        self.setWindowTitle("Dajilog USB CDC Provisioner")
        self.resize(1050, 720)
        self._build_ui()
        self.refresh_ports()

    def _build_ui(self) -> None:
        root = QWidget()
        layout = QVBoxLayout(root)

        connection = QGroupBox("Dajilog Board USB CDC · Zephyr Shell · 115200 bps")
        connection_layout = QHBoxLayout(connection)
        self.port_combo = QComboBox()
        self.refresh_button = QPushButton("포트 새로고침")
        self.connect_button = QPushButton("연결")
        self.connection_label = QLabel("연결 안 됨")
        self.connection_label.setStyleSheet("color:#b42318;font-weight:700")
        connection_layout.addWidget(QLabel("USB CDC 포트"))
        connection_layout.addWidget(self.port_combo, 1)
        connection_layout.addWidget(self.refresh_button)
        connection_layout.addWidget(self.connect_button)
        connection_layout.addWidget(self.connection_label)
        layout.addWidget(connection)

        actions = QGroupBox("보드 프로비저닝")
        actions_layout = QGridLayout(actions)
        self.info_button = QPushButton("Device Info 조회")
        self.profile_button = QPushButton("Device Profile 조회")
        self.node_id = QSpinBox()
        self.node_id.setRange(1, 127)
        self.node_id.setValue(12)
        self.set_id_button = QPushButton("Node ID 저장")
        self.reboot_button = QPushButton("보드 재부팅")
        self.status_label = QLabel("대기")
        actions_layout.addWidget(self.info_button, 0, 0)
        actions_layout.addWidget(self.profile_button, 0, 1)
        actions_layout.addWidget(QLabel("새 Node ID"), 1, 0)
        actions_layout.addWidget(self.node_id, 1, 1)
        actions_layout.addWidget(self.set_id_button, 1, 2)
        actions_layout.addWidget(self.reboot_button, 2, 0)
        actions_layout.addWidget(QLabel("상태"), 2, 1)
        actions_layout.addWidget(self.status_label, 2, 2)
        layout.addWidget(actions)

        self.table = QTableWidget(0, 3)
        self.table.setHorizontalHeaderLabels(("시간", "방향", "USB CDC / Zephyr Shell 내용"))
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.verticalHeader().setVisible(False)
        header = self.table.horizontalHeader()
        header.setSectionResizeMode(0, QHeaderView.ResizeToContents)
        header.setSectionResizeMode(1, QHeaderView.ResizeToContents)
        header.setSectionResizeMode(2, QHeaderView.Stretch)
        layout.addWidget(self.table, 1)

        footer = QHBoxLayout()
        self.clear_button = QPushButton("로그 지우기")
        footer.addStretch()
        footer.addWidget(self.clear_button)
        layout.addLayout(footer)
        self.setCentralWidget(root)

        self.refresh_button.clicked.connect(self.refresh_ports)
        self.connect_button.clicked.connect(self.toggle_connection)
        self.info_button.clicked.connect(lambda: self._send("device info"))
        self.profile_button.clicked.connect(lambda: self._send("device profile get"))
        self.set_id_button.clicked.connect(self.set_node_id)
        self.reboot_button.clicked.connect(self.reboot)
        self.clear_button.clicked.connect(lambda: self.table.setRowCount(0))
        self._set_actions_enabled(False)

    def refresh_ports(self) -> None:
        selected = self.port_combo.currentData()
        self.port_combo.clear()
        ports = list(list_ports.comports())
        ports.sort(key=lambda port: ("usbmodem" not in port.device.lower(), port.device))
        for port in ports:
            label = port.device if not port.description else f"{port.device} — {port.description}"
            self.port_combo.addItem(label, port.device)
        if selected is not None:
            index = self.port_combo.findData(selected)
            if index >= 0:
                self.port_combo.setCurrentIndex(index)
        if not self.port_combo.count():
            self.port_combo.addItem("USB CDC 포트를 찾지 못했습니다", None)

    def toggle_connection(self) -> None:
        if self.worker is not None:
            self.worker.requestInterruption()
            self.connect_button.setEnabled(False)
            return
        port = self.port_combo.currentData()
        if not port:
            QMessageBox.warning(self, "연결 불가", "Dajilog 보드의 USB CDC 포트를 연결하세요.")
            return
        worker = UsbCdcWorker(port)
        worker.line_received.connect(lambda line: self._append_log("RX", line))
        worker.command_sent.connect(lambda command: self._append_log("TX", command))
        worker.connection_changed.connect(self._connection_changed)
        worker.error_occurred.connect(lambda message: QMessageBox.critical(self, "USB CDC 오류", message))
        worker.finished.connect(self._worker_finished)
        self.worker = worker
        self.connect_button.setEnabled(False)
        self.connection_label.setText("연결 중…")
        worker.start()

    def _connection_changed(self, connected: bool, port: str) -> None:
        self.connect_button.setEnabled(True)
        self.connect_button.setText("연결 해제" if connected else "연결")
        self.connection_label.setText(f"연결됨: {port}" if connected else "연결 안 됨")
        color = "#067647" if connected else "#b42318"
        self.connection_label.setStyleSheet(f"color:{color};font-weight:700")
        self.port_combo.setEnabled(not connected)
        self.refresh_button.setEnabled(not connected)
        self._set_actions_enabled(connected)
        self.status_label.setText("USB CDC 연결됨" if connected else "대기")

    def _worker_finished(self) -> None:
        old = self.worker
        self.worker = None
        if old is not None:
            old.deleteLater()
        self._connection_changed(False, "")

    def _set_actions_enabled(self, enabled: bool) -> None:
        for widget in (
            self.info_button,
            self.profile_button,
            self.node_id,
            self.set_id_button,
            self.reboot_button,
        ):
            widget.setEnabled(enabled)

    def _send(self, command: str) -> None:
        if self.worker is not None and self.worker.isRunning():
            self.worker.send_command(command)
            self.status_label.setText(f"명령 전송: {command}")

    def set_node_id(self) -> None:
        node_id = self.node_id.value()
        answer = QMessageBox.question(
            self,
            "Node ID 저장 확인",
            f"USB로 직접 연결된 보드의 Node ID를 {node_id}(으)로 저장할까요?",
        )
        if answer == QMessageBox.Yes:
            self._send(f"device node set {node_id}")

    def reboot(self) -> None:
        answer = QMessageBox.question(self, "재부팅 확인", "연결된 보드를 재부팅할까요?")
        if answer == QMessageBox.Yes:
            self._send("device reboot")

    def _append_log(self, direction: str, message: str) -> None:
        values = [
            datetime.now().astimezone().isoformat(timespec="milliseconds"),
            direction,
            message,
        ]
        row = self.table.rowCount()
        self.table.insertRow(row)
        for column, value in enumerate(values):
            item = QTableWidgetItem(value)
            if column == 1:
                item.setForeground(QColor("#175CD3" if direction == "RX" else "#067647"))
            self.table.setItem(row, column, item)
        self.table.scrollToBottom()

    def closeEvent(self, event) -> None:  # noqa: N802
        if self.worker is not None:
            self.worker.requestInterruption()
            self.worker.wait(1500)
        event.accept()
