import os

import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtCore import QCoreApplication, QEvent
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication

from ejection_can_simulator.main_window import MainWindow, parse_interval_sequence
from ejection_can_simulator.protocol import output_frame


@pytest.fixture(scope="session")
def qapp():
    app = QApplication.instance() or QApplication([])
    yield app
    QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)
    app.processEvents()


def dispose(window, app):
    window.close()
    window.deleteLater()
    QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)
    app.processEvents()


@pytest.mark.parametrize(
    ("text", "expected"),
    [
        ("20, 10, 50, 30", [20, 10, 50, 30]),
        ("20 10 50 30", [20, 10, 50, 30]),
        ("20;10, 50\n30", [20, 10, 50, 30]),
    ],
)
def test_parse_interval_sequence(text, expected):
    assert parse_interval_sequence(text) == expected


@pytest.mark.parametrize("text", ["", "0", "10001", "20, abc"])
def test_parse_interval_sequence_rejects_invalid_values(text):
    with pytest.raises(ValueError):
        parse_interval_sequence(text)


def test_queue_ui_does_not_schedule_automatic_off(qapp):
    window = MainWindow()
    sent = []
    window._send = sent.append
    window.channels[0].setChecked(True)
    window.channels[2].setChecked(True)
    window.turn_selected_on()
    QTest.qWait(200)
    assert sent == [output_frame(5)]
    window.repeat_count.setValue(32)
    window.repeat_intervals.setText("20, 10, 50")
    window.repeat_selected()
    window.batch_timer.stop()
    while window.batch_remaining:
        window._send_batch_next()
    assert sent == [output_frame(5)] * 33
    window.all_off()
    assert sent[-1] == output_frame(0)
    assert not window.batch_remaining and not window.batch_timer.isActive()
    dispose(window, qapp)


def test_sequential_and_connection_do_not_cancel_board(qapp):
    window = MainWindow()
    sent = []
    window._send = sent.append
    window._connection_changed(True, "test")
    QTest.qWait(200)
    assert not sent
    window.repeat_count.setValue(13)
    window.repeat_intervals.setText("20 10 50 30")
    window.repeat_sequential()
    window.batch_timer.stop()
    while window.batch_remaining:
        window._send_batch_next()
    assert sent == [output_frame(1 << (i % 12)) for i in range(13)]
    window._connection_changed(False, "")
    assert not window.batch_timer.isActive()
    dispose(window, qapp)
