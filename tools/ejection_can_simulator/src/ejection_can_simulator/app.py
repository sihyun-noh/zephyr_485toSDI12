import sys

from PySide6.QtWidgets import QApplication

from ejection_can_simulator.main_window import MainWindow


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("12CH Ejection CAN Simulator")
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
