import re
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
    QLineEdit,
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


def parse_interval_sequence(text: str) -> list[int]:
    """Parse comma/whitespace separated inter-frame intervals in milliseconds."""
    tokens = [token for token in re.split(r"[\s,;]+", text.strip()) if token]
    if not tokens:
        raise ValueError("간격을 하나 이상 입력하세요.")
    try:
        intervals = [int(token) for token in tokens]
    except ValueError as exc:
        raise ValueError("간격은 정수(ms)만 입력할 수 있습니다.") from exc
    if any(interval < 1 or interval > 10000 for interval in intervals):
        raise ValueError("각 간격은 1~10000 ms 범위여야 합니다.")
    return intervals


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.worker: CanWorker | None = None
        self.output_mask = 0
        self.heartbeat_sequence = 0
        self.batch_timer = QTimer(self)
        self.batch_timer.setSingleShot(True)
        self.batch_timer.setTimerType(Qt.PreciseTimer)
        self.batch_timer.timeout.connect(self._send_batch_next)
        self.batch_masks: list[int] = []
        self.batch_intervals = [20]
        self.batch_remaining = 0
        self.batch_index = 0
        self.heartbeat_timer = QTimer(self)
        self.heartbeat_timer.setTimerType(Qt.PreciseTimer)
        self.heartbeat_timer.setInterval(100)
        self.heartbeat_timer.timeout.connect(self._send_heartbeat)
        self.channels: list[QCheckBox] = []
        self.indicators: dict[str, QLabel] = {}
        self.setWindowTitle("12CH Ejection Controller — Phase 4 예약 시험")
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

        output_box = QGroupBox("0x101 예약 등록 — MOVE/HOLD는 보드 설정 사용, PC 자동 OFF 없음")
        output_grid = QGridLayout(output_box)
        for index in range(12):
            channel = QCheckBox(f"CH{index + 1}")
            self.channels.append(channel)
            output_grid.addWidget(channel, index // 6, index % 6)
        self.on_button = QPushButton("선택 채널 1회 예약")
        self.off_selected_button = QPushButton("선택 채널 반복 예약")
        self.pulse_button = QPushButton("CH1→CH12 순차 예약")
        self.off_button = QPushButton("전체 취소 / ALL OFF")
        self.clear_button = QPushButton("선택 해제")
        self.heartbeat_box = QCheckBox("선택 기능: Heartbeat 100 ms")
        buttons = QHBoxLayout()
        for widget in (self.on_button, self.off_selected_button, self.pulse_button,
                       self.off_button, self.clear_button):
            buttons.addWidget(widget)
        buttons.addStretch()
        buttons.addWidget(self.heartbeat_box)
        output_grid.addLayout(buttons, 2, 0, 1, 6)
        self.repeat_count = QSpinBox()
        self.repeat_count.setRange(1, 1000)
        self.repeat_count.setValue(32)
        self.repeat_intervals = QLineEdit("20, 10, 50, 30, 100, 20, 56, 20, 43, 80")
        self.repeat_intervals.setPlaceholderText("예: 20, 10, 50, 30, 100")
        output_grid.addWidget(QLabel("반복 횟수"), 3, 0)
        output_grid.addWidget(self.repeat_count, 3, 1)
        output_grid.addWidget(QLabel("전송 간격 시퀀스 (ms, 반복 적용)"), 3, 2)
        output_grid.addWidget(self.repeat_intervals, 3, 3, 1, 2)
        self.batch_label = QLabel("대기")
        output_grid.addWidget(self.batch_label, 3, 5)
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
        self.off_selected_button.clicked.connect(self.repeat_selected)
        self.pulse_button.clicked.connect(self.repeat_sequential)
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
            self.batch_timer.stop()
            self.batch_remaining = 0
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
        if not connected:
            self.batch_timer.stop()
            self.batch_remaining = 0
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

    def repeat_selected(self) -> None:
        mask = self.selected_mask()
        if mask == 0:
            QMessageBox.warning(self, "채널 미선택", "출력 채널을 하나 이상 선택하세요.")
            return
        self._start_batch([mask])

    def repeat_sequential(self) -> None:
        self._start_batch([1 << index for index in range(12)])

    def _start_batch(self, masks: list[int]) -> None:
        self.batch_timer.stop()
        try:
            self.batch_intervals = parse_interval_sequence(self.repeat_intervals.text())
        except ValueError as exc:
            QMessageBox.warning(self, "전송 간격 오류", str(exc))
            return
        self.batch_masks = masks
        self.batch_index = 0
        self.batch_remaining = self.repeat_count.value()
        self._send_batch_next()

    def _send_batch_next(self) -> None:
        if not self.batch_remaining:
            self.batch_timer.stop()
            return
        self._send(output_frame(self.batch_masks[self.batch_index % len(self.batch_masks)]))
        self.batch_index += 1
        self.batch_remaining -= 1
        if not self.batch_remaining:
            self.batch_timer.stop()
            self.batch_label.setText(f"전송 {self.batch_index} / 완료")
            return
        interval = self.batch_intervals[(self.batch_index - 1) % len(self.batch_intervals)]
        self.batch_label.setText(
            f"전송 {self.batch_index} / 남음 {self.batch_remaining} / 다음 {interval} ms"
        )
        self.batch_timer.start(interval)

    def turn_selected_on(self) -> None:
        mask = self.selected_mask()
        if mask == 0:
            QMessageBox.warning(self, "채널 미선택", "출력 채널을 하나 이상 선택하세요.")
            return
        self._send(output_frame(mask))

    def all_off(self) -> None:
        self.batch_timer.stop()
        self.batch_remaining = 0
        self.batch_label.setText("전체 취소 요청")
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
            return f"예약 / 접수 Echo mask=0x{mask:03X}" if mask else "전체 취소 / Echo"
        if frame.arbitration_id == HEARTBEAT_ID:
            return "Optional heartbeat"
        return "기타 프레임"

    def _log(self, direction: str, frame: CanFrame, description: str) -> None:
        if self.table.rowCount() >= 1000:
            self.table.removeRow(0)
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
        self.batch_timer.stop()
        if self.worker is not None:
            self.worker.requestInterruption()
            self.worker.wait(1500)
        event.accept()
