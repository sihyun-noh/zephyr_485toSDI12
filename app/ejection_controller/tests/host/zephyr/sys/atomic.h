#ifndef TEST_ATOMIC_H
#define TEST_ATOMIC_H
typedef int atomic_t;
static inline int atomic_get(const atomic_t *v) { return *v; }
static inline int atomic_inc(atomic_t *v) { return (*v)++; }
#endif
