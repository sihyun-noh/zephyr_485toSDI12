#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/net/net_core.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>

#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include "k_poll_event.h"
#include "wifi_state.h"

LOG_MODULE_REGISTER(wifi, CONFIG_LOG_DEFAULT_LEVEL);

static struct net_mgmt_event_callback wifi_mgmt_cb;

struct k_poll_signal wifi_connected_signal =
    K_POLL_SIGNAL_INITIALIZER(wifi_connected_signal);
struct k_poll_event wifi_poll_event = K_POLL_EVENT_INITIALIZER(
    K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &wifi_connected_signal);

void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                             uint32_t mgmt_event, struct net_if *iface) {
  if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
    struct wifi_status *status = (struct wifi_status *)cb->info;

    if (status->status == WIFI_STATUS_CONN_SUCCESS) {
      LOG_INF("wifi Connected");
      k_poll_signal_raise(&wifi_connected_signal, WIFI_SIGNAL_SUCCESS);
    } else {
      LOG_INF("wifi Connected failed");
      k_poll_signal_raise(&wifi_connected_signal, WIFI_SIGNAL_FAILURE);
    }
  }

  if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
    LOG_INF("wifi Disconnected");
  }
}

void reg_cb_wifi_state() {
  net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler,
                               NET_EVENT_WIFI_CONNECT_RESULT |
                                   NET_EVENT_WIFI_DISCONNECT_RESULT);
  net_mgmt_add_event_callback(&wifi_mgmt_cb);
  return;
}
