#ifndef MODBUS_H
#define MODBUS_H

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define UART2_NODE DT_NODELABEL(uart2)

void modbus_init(void);
void modbus_send_message(const char *msg);
void modbus_receive_loop(void);
void modbus_app_start(void);

#endif
