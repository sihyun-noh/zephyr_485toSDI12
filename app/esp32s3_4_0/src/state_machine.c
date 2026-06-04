#include <zephyr/logging/log.h>
#include <zephyr/smf.h>

#include "state_machine.h"

LOG_MODULE_REGISTER(smf_test, CONFIG_LOG_DEFAULT_LEVEL);

/* Forward declaration of state table */
static const struct smf_state demo_states[];

/* List of demo states */
enum demo_state { PARENT, S0, S1, S2 };

/* User defined object */
struct s_object {
  /* This must be first */
  struct smf_ctx ctx;

  /* Other state specific data add here */
} s_obj;

/* Parent State */
static void parent_entry(void *o) {

  LOG_INF("parent_entry");
  /* Do something */
}
static void parent_exit(void *o) {
  LOG_INF("parent_exit");
  /* Do something */
}

/* State S0 */
static void s0_entry(void *o) {
  /* Do something */
  LOG_INF("s0 entry");
}
static void s0_run(void *o) {
  LOG_INF("s0 run");
  smf_set_state(SMF_CTX(&s_obj), &demo_states[S1]);
}
static void s0_exit(void *o) {
  LOG_INF("s0 exit");
  /* Do something */
}

/* State S1 */
static void s1_run(void *o) {
  LOG_INF("s1 run");
  smf_set_state(SMF_CTX(&s_obj), &demo_states[S2]);
}
static void s1_exit(void *o) {
  LOG_INF("s1 exit");
  /* Do something */
}

/* State S2 */
static void s2_entry(void *o) {

  LOG_INF("s2 entry");
  /* Do something */
}
static void s2_run(void *o) {
  LOG_INF("s2 run");
  smf_set_state(SMF_CTX(&s_obj), &demo_states[S0]);
}

/* Populate state table */
static const struct smf_state demo_states[] = {
    /* Parent state does not have a run action */
    [PARENT] = SMF_CREATE_STATE(parent_entry, NULL, parent_exit, NULL, NULL),
    [S0] =
        SMF_CREATE_STATE(s0_entry, s0_run, s0_exit, &demo_states[PARENT], NULL),
    /* State S1 does not have an entry action */
    [S1] = SMF_CREATE_STATE(NULL, s1_run, s1_exit, NULL, NULL),
    /* State S2 does not have an exit action */
    [S2] = SMF_CREATE_STATE(s2_entry, s2_run, NULL, &demo_states[PARENT], NULL),
};
int start_smf(void) {
  int32_t ret;

  /* Set initial state */
  smf_set_initial(SMF_CTX(&s_obj), &demo_states[S0]);

  /* Run the state machine */
  while (1) {
    /* State machine terminates if a non-zero value is returned */
    ret = smf_run_state(SMF_CTX(&s_obj));
    if (ret) {
      /* handle return code and terminate state machine */
      break;
    }
    k_msleep(1000);
  }
  return 0;
}
