#include "sys_os_init.h"
#include "zephyr/kernel/thread_stack.h"
#include "zephyr/logging/log.h"
#include <zephyr/kernel.h>

SYS_OS_LOG(sample_thread);

void *example_callback(void *user_data) {
  LOG_INF("Callback executed\n");
  return NULL;
}

#define MY_STACK_SIZE 500
#define MY_PRIORITY_6 6
#define MY_PRIORITY_7 7
#define MY_PRIORITY_8 8
#define MY_PRIORITY_5 5

extern void my_entry_point(void *, void *, void *);

K_THREAD_DEFINE(my_tid, MY_STACK_SIZE, my_entry_point, NULL, NULL, NULL,
                MY_PRIORITY_5, 0, 0);

///////////////////////////////////////////////////////////////////////////////
////////////////////////   Work queue  ////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
K_THREAD_STACK_DEFINE(high_priority_stack_area, MY_STACK_SIZE);
K_THREAD_STACK_DEFINE(low_priority_stack_area, MY_STACK_SIZE);

struct k_work_q high_priority_work_q;
struct k_work_q low_priority_work_q;

static struct k_work my_work_1;
static struct k_work my_work_2;

struct device_info {
  struct k_work work;
  char name[16];
} my_device;

///////////////////////////////////////////////////////////////////////////////
////////////////////////   Work queue  ////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void print_error(struct k_work *item) {
  struct device_info *the_device = CONTAINER_OF(item, struct device_info, work);
  printk("Got error on device %s\n", the_device->name);
}
void my_work_handler(struct k_work *work) {
  printk("Executing work handler\n");
  LOG_INF("Work item is being processed in the work queue\n");
}
void my_work_handler_2(struct k_work *work) {
  printk("Executing work handler\n");
  LOG_INF("Work item is being processed in the work queue_2\n");
}

void my_entry_point(void *, void *, void *) {

  k_work_queue_init(&high_priority_work_q);
  k_work_queue_init(&low_priority_work_q);
  k_work_queue_start(&high_priority_work_q, high_priority_stack_area,
                     K_THREAD_STACK_SIZEOF(high_priority_stack_area),
                     MY_PRIORITY_8, NULL);
  k_work_queue_start(&low_priority_work_q, low_priority_stack_area,
                     K_THREAD_STACK_SIZEOF(low_priority_stack_area),
                     MY_PRIORITY_5, NULL);

  k_work_init(&my_work_1, my_work_handler);
  k_work_init(&my_work_2, my_work_handler_2);
  k_work_submit_to_queue(&high_priority_work_q, &my_work_1);
  k_work_submit_to_queue(&low_priority_work_q, &my_work_2);

  printk("Work Queue Example\n");

  // Initialize the work item with the handler function
  k_work_init(&my_work_1, my_work_handler);

  // Submit the work item to the system work queue
  k_work_submit(&my_work_1);

  // Sleep for demonstration purposes
  k_sleep(K_SECONDS(2));

  // Re-submit the work item to demonstrate re-use
  k_work_submit(&my_work_1);

  k_sleep(K_SECONDS(2));
  k_work_submit(&my_work_1);

  while (1) {
    k_sleep(K_SECONDS(1));
  }
}
