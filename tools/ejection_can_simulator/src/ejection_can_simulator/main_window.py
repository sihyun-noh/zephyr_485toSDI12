from datetime import datetime

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)
from serial.tools import list_ports

from ejection_can_simulator.can_adapter import CanFrame, CanWorker
from ejection_can_simulator.protocol import (
    HEARTBEAT_ID,
    OUTPUT_ID,
    STATUS_ID,
    EjectionStatus,
    heartbeat_frame,
    output_frame,
    parse_status,
)


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.worker: CanWorker | None = None
        self.output_mask = 0
        self.heartbeat_sequence = 0
        self.heartbeat_timer = QTimer(self)
        self.heartbeat_timer.setTimerType(Qt.PreciseTimer)
        self.heartbeat_timer.setInterval(100)
        self.heartbeat_timer.timeout.connect(self._send_heartbeat)
        self.channels: list[QCheckBox] = []
        self.indicators: dict[str, QLabel] = {}
        self.setWindowTitle("12CH Ejection Controller CAN Simulator")
        self.resize(1120, 760)
        self._build_ui()
        self.refresh_ports()

    def _build_ui(self) -> None:
        root = QWidget()
        layout = QVBoxLayout(root)
        connection = QGroupBox("Waveshare USB-CAN-A (Standard 11-bit)")
        row = QHBoxLayout(connection)
        self.port_combo = QComboBox()
        self.bitrate_combo = QComboBox()
        for label, bitrate in (("125 kbit/s", 125_000), ("250 kbit/s", 250_000),
                               ("500 kbit/s", 500_000), ("1 Mbit/s", 1_000_000)):
            self.bitrate_combo.addItem(label, bitrate)
        self.bitrate_combo.setCurrentIndex(1)
        self.refresh_button = QPushButton("포트 새로고침")
        self.connect_button = QPushButton("연결")
        self.connection_label = QLabel("연결 안 됨")
        self.connection_label.setStyleSheet("color:#b42318;font-weight:700")
        row.addWidget(QLabel("시리얼 포트"))
        row.addWidget(self.port_combo, 1)
        row.addWidget(QLabel("CAN 속도"))
        row.addWidget(self.bitrate_combo)
        row.addWidget(self.refresh_button)
        row.addWidget(self.connect_button)
        row.addWidget(self.connection_label)
        layout.addWidget(connection)

        status_box = QGroupBox("Controller Status (0x201)")
        status_grid = QGridLayout(status_box)
        for index, name in enumerate(("FSM", "Fault", "Sequence", "Inputs", "Outputs", "Health")):
            value = QLabel("-")
            value.setStyleSheet("font-weight:700")
            self.indicators[name] = value
            status_grid.addWidget(QLabel(name), index // 3, (index % 3) * 2)
            status_grid.addWidget(value, index // 3, (index % 3) * 2 + 1)
        layout.addWidget(status_box)

        output_box = QGroupBox("Output command (0x101)")
        output_grid = QGridLayout(output_box)
        for index in range(12):
            channel = QCheckBox(f"CH{index + 1}")
            self.channels.append(channel)
            output_grid.addWidget(channel, index // 6, index % 6)
        self.on_button = QPushButton("선택 채널 ON")
        self.off_selected_button = QPushButton("선택 채널 OFF")
        self.pulse_button = QPushButton("선택 채널 100 ms Pulse")
        self.off_button = QPushButton("ALL OFF")
        self.clear_button = QPushButton("선택 해제")
        self.heartbeat_box = QCheckBox("선택 기능: Heartbeat 100 ms")
        buttons = QHBoxLayout()
        for widget in (self.on_button, self.off_selected_button, self.pulse_button,
                       self.off_button, self.clear_button):
            buttons.addWidget(widget)
        buttons.addStretch()
        buttons.addWidget(self.heartbeat_box)
        output_grid.addLayout(buttons, 2, 0, 1, 6)
        layout.addWidget(output_box)

        self.table = QTableWidget(0, 6)
        self.table.setHorizontalHeaderLabels(("시간", "방향", "CAN ID", "DLC", "Data", "해석"))
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.verticalHeader().setVisible(False)
        header = self.table.horizontalHeader()
        header.setSectionResizeMode(QHeaderView.ResizeToContents)
        header.setSectionResizeMode(5, QHeaderView.Stretch)
        layout.addWidget(self.table, 1)
        self.setCentralWidget(root)

        self.refresh_button.clicked.connect(self.refresh_ports)
        self.connect_button.clicked.connect(self.toggle_connection)
        self.on_button.clicked.connect(self.turn_selected_on)
        self.off_selected_button.clicked.connect(self.turn_selected_off)
        self.pulse_button.clicked.connect(self.pulse_selected)
        self.off_button.clicked.connect(self.all_off)
        self.clear_button.clicked.connect(lambda: [channel.setChecked(False) for channel in self.channels])
        self.heartbeat_box.toggled.connect(self.toggle_heartbeat)
        self._set_controls(False)

    def refresh_ports(self) -> None:
        selected = self.port_combo.currentData()
        self.port_combo.clear()
        for port in list_ports.comports():
            label = port.device if not port.description else f"{port.device} — {port.description}"
            self.port_combo.addItem(label, port.device)
        if selected:
            index = self.port_combo.findData(selected)
            if index >= 0:
                self.port_combo.setCurrentIndex(index)
        if not self.port_combo.count():
            self.port_combo.addItem("시리얼 포트를 찾지 못했습니다", None)

    def toggle_connection(self) -> None:
        if self.worker is not None:
            self.worker.requestInterruption()
            self.connect_button.setEnabled(False)
            return
        port = self.port_combo.currentData()
        if not port:
            QMessageBox.warning(self, "연결 불가", "USB-CAN 장치를 연결하고 새로고침하세요.")
            return
        worker = CanWorker(port, self.bitrate_combo.currentData())
        worker.frame_received.connect(self._received)
        worker.frame_sent.connect(lambda frame: self._log("TX", frame, self._describe(frame)))
        worker.connection_changed.connect(self._connection_changed)
        worker.error_occurred.connect(lambda text: QMessageBox.critical(self, "CAN 오류", text))
        worker.finished.connect(self._worker_finished)
        self.worker = worker
        self.connect_button.setEnabled(False)
        self.connection_label.setText("연결 중…")
        worker.start()

    def _connection_changed(self, connected: bool, port: str) -> None:
        self.connect_button.setEnabled(True)
        self.connect_button.setText("연결 해제" if connected else "연결")
        self.connection_label.setText(f"연결됨: {port}" if connected else "연결 안 됨")
        self.connection_label.setStyleSheet(
            f"color:{'#067647' if connected else '#b42318'};font-weight:700"
        )
        self.port_combo.setEnabled(not connected)
        self.bitrate_combo.setEnabled(not connected)
        self.refresh_button.setEnabled(not connected)
        self._set_controls(connected)
        if connected:
            QTimer.singleShot(150, self.all_off)
        else:
            self.heartbeat_timer.stop()
            self.heartbeat_box.setChecked(False)

    def _worker_finished(self) -> None:
        old = self.worker
        self.worker = None
        if old is not None:
            old.deleteLater()
        self._connection_changed(False, "")

    def _set_controls(self, enabled: bool) -> None:
        for widget in (
            self.on_button, self.off_selected_button, self.pulse_button, self.off_button,
            self.clear_button, self.heartbeat_box,
        ):
            widget.setEnabled(enabled)

    def selected_mask(self) -> int:
        return sum(1 << index for index, channel in enumerate(self.channels) if channel.isChecked())

    def pulse_selected(self) -> None:
        mask = self.selected_mask()
        if mask == 0:
            QMessageBox.warning(self, "채널 미선택", "출력 채널을 하나 이상 선택하세요.")
            return
        self._send(output_frame(mask))
        self.output_mask = 0
        if self.worker is not None and self.worker.isRunning():
            self.worker.enqueue_after(output_frame(0), 100)

    def turn_selected_on(self) -> None:
        mask = self.selected_mask()
        if mask == 0:
            QMessageBox.warning(self, "채널 미선택", "출력 채널을 하나 이상 선택하세요.")
            return
        self.output_mask |= mask
        self._send(output_frame(self.output_mask))

    def turn_selected_off(self) -> None:
        selected = self.selected_mask()
        if selected == 0:
            QMessageBox.warning(self, "채널 미선택", "OFF할 채널을 하나 이상 선택하세요.")
            return
        self.output_mask &= ~selected
        self._send(output_frame(self.output_mask))

    def all_off(self) -> None:
        self.output_mask = 0
        self._send(output_frame(0))

    def toggle_heartbeat(self, enabled: bool) -> None:
        if enabled:
            self.heartbeat_timer.start()
        else:
            self.heartbeat_timer.stop()

    def _send_heartbeat(self) -> None:
        self._send(heartbeat_frame(self.heartbeat_sequence))
        self.heartbeat_sequence = (self.heartbeat_sequence + 1) & 0xFF

    def _send(self, frame: CanFrame) -> None:
        if self.worker is not None and self.worker.isRunning():
            self.worker.enqueue(frame)

    def _received(self, frame: CanFrame) -> None:
        if frame.arbitration_id == STATUS_ID:
            try:
                status = parse_status(frame)
            except ValueError as exc:
                self._log("RX", frame, str(exc))
                return
            self._update_status(status)
            self._log("RX", frame, f"{status.state_name} · {status.fault_name}")
        else:
            self._log("RX", frame, self._describe(frame))

    def _update_status(self, status: EjectionStatus) -> None:
        self.output_mask = status.outputs
        self.indicators["FSM"].setText(f"{status.state_name} ({status.state_code})")
        self.indicators["Fault"].setText(f"{status.fault_name} ({status.fault_code})")
        self.indicators["Sequence"].setText(str(status.sequence))
        self.indicators["Inputs"].setText(f"0x{status.inputs:04X}")
        self.indicators["Outputs"].setText(f"0x{status.outputs:03X}")
        self.indicators["Health"].setText(
            f"SET={int(status.settings_ready)} CAN={int(status.can_ready)} "
            f"ENC={int(status.encoder_ready)} OLED={int(status.oled_ready)}"
        )

    @staticmethod
    def _describe(frame: CanFrame) -> str:
        if frame.arbitration_id == OUTPUT_ID:
            mask = frame.data[0] | (frame.data[1] << 8) if len(frame.data) >= 2 else 0
            return f"Output mask=0x{mask:03X}"
        if frame.arbitration_id == HEARTBEAT_ID:
            return "Optional heartbeat"
        return "기타 프레임"

    def _log(self, direction: str, frame: CanFrame, description: str) -> None:
        row = self.table.rowCount()
        self.table.insertRow(row)
        values = (
            datetime.now().astimezone().isoformat(timespec="milliseconds"),
            direction,
            f"0x{frame.arbitration_id:03X}",
            str(len(frame.data)),
            frame.data.hex(" ").upper(),
            description,
        )
        for column, value in enumerate(values):
            item = QTableWidgetItem(value)
            if column == 1:
                item.setForeground(QColor("#175CD3" if direction == "RX" else "#067647"))
            self.table.setItem(row, column, item)
        self.table.scrollToBottom()

    def closeEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        self.heartbeat_timer.stop()
        if self.worker is not None:
            self.worker.enqueue(output_frame(0))
            self.worker.requestInterruption()
            self.worker.wait(1500)
        event.accept()
