/*
 * Configuration each task
 *
 *
 *
 * */

#ifndef __SYS_OS_INIT_H__
#define __SYS_OS_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/logging/log.h>

#define SYS_OS_LOG(module) LOG_MODULE_REGISTER(module, CONFIG_LOG_DEFAULT_LEVEL)

typedef enum {
  SYS_OS_ERROR = 0x00,
  SYS_OS_SUCCESS = 0x01,
  SYS_OS_REG_SUCCESS,
  SYS_OS_EXCEPTION_HANDLING_1 = 0xC0,
} sys_os_state_t;

typedef enum {
  SYS_OS_TASK_ID_NONE = 0x00,
  SYS_OS_TASK_ID_CONTROL,
  SYS_OS_TASK_ID_SENSOR,
} sys_os_task_id_t;

typedef struct user_data sys_os_user_data_t;

typedef void *(*sys_os_cb_func_t)(void *);

typedef struct {
  void *config;
} zbus_t;

typedef struct {
  zbus_t conig;
} sys_os_zbus_msg_t;

typedef struct {
  sys_os_task_id_t task_id;
  sys_os_user_data_t *user_data;
  sys_os_cb_func_t cb;
  sys_os_zbus_msg_t z_bus_msg;
} sys_os_task_prep_t;

sys_os_state_t sys_os_task_init(sys_os_task_prep_t task_info);

sys_os_state_t sys_os_task_register(sys_os_task_prep_t task);

sys_os_state_t sys_os_task_event_cb_handler(sys_os_cb_func_t cb);

sys_os_state_t sys_os_task_msg(sys_os_zbus_msg_t msg_info,
                               sys_os_user_data_t data);

#ifdef __cplusplus
}
#endif

#endif
