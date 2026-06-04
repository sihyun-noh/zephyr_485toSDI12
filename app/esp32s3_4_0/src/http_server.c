#include "http_server.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(http_server, CONFIG_LOG_DEFAULT_LEVEL);

#define HTTP_PORT 80
#define MAX_CLIENTS 1
#define BUFFER_SIZE 1024

static const char http_response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html>"
    "<html>"
    "<head><title>ESP32 Web Server</title></head>"
    "<body>"
    "<h1>Welcome to ESP32 Web Server</h1>"
    "<p>This is a simple web server running on ESP32.</p>"
    "</body>"
    "</html>";

static void handle_client(int client_sock) {
  char buffer[BUFFER_SIZE];
  int received;

  // 클라이언트로부터 요청 받기
  received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
  if (received < 0) {
    LOG_ERR("Error receiving data: %d", errno);
    goto cleanup;
  }

  buffer[received] = '\0';
  LOG_INF("Received request:\n%s", buffer);

  // HTTP 응답 보내기
  if (send(client_sock, http_response, sizeof(http_response) - 1, 0) < 0) {
    LOG_ERR("Error sending response: %d", errno);
  }

cleanup:
  close(client_sock);
}

void print_ip_address(void) {
  struct net_if *iface = net_if_get_default();
  if (iface == NULL) {
    LOG_ERR("No default network interface found.");
    return;
  }

  struct net_if_ipv4 *ipv4_config = iface->config.ip.ipv4;
  if (ipv4_config == NULL) {
    LOG_ERR("No IPv4 configuration found for the interface.");
    return;
  }

  if (ipv4_config->unicast[0].ipv4.address.family == AF_INET) {
    char ip_str[NET_IPV4_ADDR_LEN];
    net_addr_ntop(AF_INET, &ipv4_config->unicast[0].ipv4.address.in_addr,
                  ip_str, sizeof(ip_str));
    LOG_INF("IP Address: %s", ip_str);
  } else {
    LOG_ERR("No valid IPv4 address found.");
  }
}

int start_http_server(void) {
  int server_sock;
  struct sockaddr_in server_addr;
  struct net_if *iface = net_if_get_default();
  struct net_if_ipv4 *ipv4;
  char ip_str[NET_IPV4_ADDR_LEN];

  // 소켓 생성
  server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server_sock < 0) {
    LOG_ERR("Failed to create socket: %d", errno);
    return -errno;
  }

  if (iface != NULL) {
    if (net_if_config_ipv4_get(iface, &ipv4) == 0 && ipv4 != NULL) {

      net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr, ip_str,
                    sizeof(ip_str));
      printf("IP Address: %s\n", ip_str);
    } else {
      printf("IPv4 configuration is not available.\n");
    }
  } else {
    printf("Default network interface is not available.\n");
    strcpy(ip_str, "unknown");
  }

  // if (iface && iface->config.ip.ipv4) {
  //   net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[0].addr, ip_str,
  //                 sizeof(ip_str));

  // } else {
  //   strcpy(ip_str, "unknown");
  // }

  // 서버 주소 설정
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(HTTP_PORT);

  // 소켓에 주소 바인딩
  if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    LOG_ERR("Failed to bind socket: %d", errno);
    close(server_sock);
    return -errno;
  }

  // 연결 대기 시작
  if (listen(server_sock, MAX_CLIENTS) < 0) {
    LOG_ERR("Failed to listen: %d", errno);
    close(server_sock);
    return -errno;
  }

  LOG_INF("HTTP server started on http://%s:%d", ip_str, HTTP_PORT);

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sock;

    // 클라이언트 연결 수락
    client_sock =
        accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_sock < 0) {
      LOG_ERR("Failed to accept connection: %d", errno);
      continue;
    }

    LOG_INF("New client connected");
    handle_client(client_sock);
  }

  return 0;
}
