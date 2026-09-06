# Dajilog 485 to SDI-12 Bridge

This application bridges an RS485 Modbus sensor to an SDI-12 slave interface.

## Build

```bash
cd /Users/sihyun.noh/project/zephyr_workspace/my-workspace/example-application
source ~/zephyrproject/.venv/bin/activate

west build -p always \
  -b dajilog_tech \
  app/dajilog_485_to_sdi12 \
  -d build_dajilog_485_to_sdi12_default \
  -- -DEXTRA_CONF_FILE=profiles/sensor_default.conf
```

## Flash

```bash
west flash -d build_dajilog_485_to_sdi12_default
```
