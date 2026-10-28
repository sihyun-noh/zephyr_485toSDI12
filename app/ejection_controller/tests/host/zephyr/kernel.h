/* Minimal single-thread test double for EEPROM settings unit tests only. */
#ifndef TEST_KERNEL_H
#define TEST_KERNEL_H
#include <stdint.h>
#include <stddef.h>
#define ARG_UNUSED(x) (void)(x)
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define K_NO_WAIT 0
#define K_MSEC(n) (n)
#define K_SEM_DEFINE(name, initial, limit) int name
#define K_THREAD_STACK_DEFINE(name, size) char name[size]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)
struct k_thread { int unused; };
extern int64_t test_now;
static inline int64_t k_uptime_get(void) { return test_now; }
static inline void k_sem_give(int *s) { (void)s; }
static inline int k_sem_take(int *s, int64_t t) { (void)s; (void)t; return 0; }
static inline void k_thread_create(struct k_thread *t, char *stack, size_t size,
    void (*entry)(void *, void *, void *), void *a, void *b, void *c,
    int priority, int options, int timeout)
{
    (void)t; (void)stack; (void)size; (void)entry; (void)a; (void)b; (void)c;
    (void)priority; (void)options; (void)timeout;
}
static inline void k_thread_name_set(struct k_thread *t, const char *name)
{ (void)t; (void)name; }
#define BUILD_ASSERT(c) _Static_assert(c, #c)
#define K_FOREVER (-1)
#define K_MUTEX_DEFINE(name) int name
static inline void k_mutex_lock(int *m, int t) { (void)m; (void)t; }
static inline void k_mutex_unlock(int *m) { (void)m; }
#endif
