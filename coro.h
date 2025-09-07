#ifndef __CORO_H__
#define __CORO_H__

#include <ucontext.h>

#define CORO_STACK_SIZE 1024 * 64

typedef enum {
    CORO_READY,
    CORO_RUNNING,
    CORO_SUSPENDED,
    CORO_SLEEPING,
    CORO_FINISHED,
} CoroState;

typedef struct {
    void (*func)(void *);
    void *arg;
} CoroEntry;

typedef struct Coro Coro;

struct Coro {
    ucontext_t ctx;
    CoroEntry entry;
    void *stack;
    CoroState state;
    int waiting_fd;
    int waiting_events;
};

int coro_create(void (*func)(void *), void *arg);
void coro_start(void);
void coro_yield(void);
void coro_sleep_fd(int fd, int events);
void coro_sleep_ms(int ms);

#endif
