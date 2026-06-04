#ifndef _C_MQTT_H__
#define _C_MQTT_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_NET_CONFIG_SETTINGS
#ifdef CONFIG_NET_IPV6
#define ZEPHYR_ADDR CONFIG_NET_CONFIG_MY_IPV6_ADDR
#define SERVER_ADDR CONFIG_NET_CONFIG_PEER_IPV6_ADDR
#else
#define ZEPHYR_ADDR CONFIG_NET_CONFIG_MY_IPV4_ADDR
#define SERVER_ADDR CONFIG_NET_CONFIG_PEER_IPV4_ADDR
#endif
#else
#ifdef CONFIG_NET_IPV6
#define ZEPHYR_ADDR "2001:db8::1"
#define SERVER_ADDR "2001:db8::2"
#else
#define ZEPHYR_ADDR "192.168.50.231"
#define SERVER_ADDR "192.168.50.34"
#endif
#endif

#if defined(CONFIG_SOCKS)
#define SOCKS5_PROXY_ADDR SERVER_ADDR
#define SOCKS5_PROXY_PORT 1080
#endif

#ifdef CONFIG_MQTT_LIB_TLS
#ifdef CONFIG_MQTT_LIB_WEBSOCKET
#define SERVER_PORT 9001
#else
#define SERVER_PORT 8883
#endif /* CONFIG_MQTT_LIB_WEBSOCKET */
#else
#ifdef CONFIG_MQTT_LIB_WEBSOCKET
#define SERVER_PORT 9001
#else
#define SERVER_PORT 1883
#endif /* CONFIG_MQTT_LIB_WEBSOCKET */
#endif

#define APP_CONNECT_TIMEOUT_MS 2000
#define APP_SLEEP_MSECS 1000

#define APP_CONNECT_TRIES 10

#define APP_MQTT_BUFFER_SIZE 128

#define MQTT_CLIENTID "zephyr_publisher"

/* Set the following to 1 to enable the Bluemix topic format */
#define APP_BLUEMIX_TOPIC 0

/* These are the parameters for the Bluemix topic format */
#if APP_BLUEMIX_TOPIC
#define BLUEMIX_DEVTYPE "sensor"
#define BLUEMIX_DEVID "carbon"
#define BLUEMIX_EVENT "status"
#define BLUEMIX_FORMAT "json"
#endif

int start_app(void);

int mqtt_main(void);

#ifdef __cplusplus
}
#endif

#endif
