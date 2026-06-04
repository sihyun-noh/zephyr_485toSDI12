#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdio.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/rtc/maxim_ds3231.h>

#include "rtc.h"

LOG_MODULE_REGISTER(rtc, CONFIG_LOG_DEFAULT_LEVEL);

const struct device *const ds3231 = DEVICE_DT_GET(DT_ALIAS(rtc));
/* Format times as: YYYY-MM-DD HH:MM:SS DOW DOY */
static const char *format_time(time_t time, long nsec) {
  static char buf[64];
  char *bp = buf;
  char *const bpe = bp + sizeof(buf);
  struct tm tv;
  struct tm *tp = gmtime_r(&time, &tv);

  bp += strftime(bp, bpe - bp, "%Y-%m-%d %H:%M:%S", tp);
  if (nsec >= 0) {
    bp += snprintf(bp, bpe - bp, ".%09lu", nsec);
  }
  bp += strftime(bp, bpe - bp, " %a %j", tp);
  return buf;
}

static void show_counter(const struct device *ds3231) {
  uint32_t now = 0;

  printk("\nCounter at %p\n", ds3231);
  printk("\tMax top value: %u (%08x)\n", counter_get_max_top_value(ds3231),
         counter_get_max_top_value(ds3231));
  printk("\t%u channels\n", counter_get_num_of_channels(ds3231));
  printk("\t%u Hz\n", counter_get_frequency(ds3231));

  printk("Top counter value: %u (%08x)\n", counter_get_top_value(ds3231),
         counter_get_top_value(ds3231));

  (void)counter_get_value(ds3231, &now);

  printk("Now %u: %s\n", now, format_time(now, -1));
}

int ds3231_rtc_init() {
  if (!device_is_ready(ds3231)) {
    printk("%s: device not ready.\n", ds3231->name);
    return -1;
  }

  uint32_t syncclock_Hz = maxim_ds3231_syncclock_frequency(ds3231);

  printk("DS3231 on %s syncclock %u Hz\n\n", CONFIG_BOARD, syncclock_Hz);

  int rc = maxim_ds3231_stat_update(ds3231, 0, MAXIM_DS3231_REG_STAT_OSF);

  if (rc >= 0) {
    printk("DS3231 has%s experienced an oscillator fault\n",
           (rc & MAXIM_DS3231_REG_STAT_OSF) ? "" : " not");
  } else {
    printk("DS3231 stat fetch failed: %d\n", rc);
    return -1;
  }

  /* Show the DS3231 counter properties */
  show_counter(ds3231);

  /* Show the DS3231 ctrl and ctrl_stat register values */
  printk("\nDS3231 ctrl %02x ; ctrl_stat %02x\n",
         maxim_ds3231_ctrl_update(ds3231, 0, 0),
         maxim_ds3231_stat_update(ds3231, 0, 0));

  return 0;
}
