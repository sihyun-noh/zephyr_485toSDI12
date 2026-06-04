
#include "modbus.h"

static const struct device *uart2_dev = NULL;

void modbus_init(void) {
  uart2_dev = DEVICE_DT_GET(UART2_NODE);

  if (!device_is_ready(uart2_dev)) {
    printk("UART2 device not ready!\n");
    return;
  }

  printk("UART2 ready!\n");
}

void modbus_send_message(const char *msg) {
  if (!uart2_dev) {
    printk("UART2 not initialized!\n");
    return;
  }

  for (const char *p = msg; *p; p++) {
    uart_poll_out(uart2_dev, *p);
  }
}

void modbus_receive_loop(void) {
  if (!uart2_dev) {
    printk("UART2 not initialized!\n");
    return;
  }

  while (1) {
    unsigned char c;
    if (uart_poll_in(uart2_dev, &c) == 0) {
      printk("RX: %c\n", c);
    }
    k_msleep(10);
  }
}

void modbus_app_start(void) {
  modbus_init();
  modbus_send_message("Hello Modbus!\r\n");
  modbus_receive_loop();
}
