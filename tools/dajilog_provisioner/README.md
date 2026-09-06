# Dajilog Provisioner

macOS GUI for provisioning a directly connected Dajilog board through its USB
CDC Zephyr shell. CAN is used after provisioning to discover and operate the
board; it is not the Phase 1 Node ID write channel.

```sh
./install.sh
./run.sh
```

`install.sh` creates `.venv`, installs PySide6 and pyserial, and runs the tests.
`run.sh` opens the Dajilog PC Provisioner GUI.

The GUI sends these common shell commands:

```text
device info
device profile get
device node set <1..127>
device reboot
```
