#ifndef __CORO_H__
#define __CORO_H__

#include <ucontext.h>
#include "dyn_arr.h"

#define CORO_STACK_SIZE (1024 * 64)

typedef enum {
    CORO_READY,
    CORO_RUNNING,
    CORO_SUSPENDED,
    CORO_SLEEPING,
    CORO_FINISHED,
} CoroState;

typedef struct {
    void *(*func)(void *);
    void *arg;
} CoroEntry;

typedef struct Coro Coro;

struct Coro {
    ucontext_t ctx;
    CoroEntry entry;
    void *stack;
    void *result;
    Coro *waiting_coro;
    CoroState state;
    int detached;
    int waiting_fd;
    int waiting_events;
    int timeout_fd;
    int timed_out;
};


Coro *coro_spawn(void *(*func)(void *), void *arg);
void *coro_await(Coro *target);
void coro_detach(Coro *target);
void coro_yield(void);
void coro_sleep_fd(int fd, int events);
void coro_sleep_ms(int ms);
int coro_sleep_fd_timeout(int fd, int events, int timeout_ms);

typedef struct {
    void **buf;
    size_t cap;
    size_t size;
    size_t head;
    size_t tail;

    DynArr(Coro *) waiting_send;
    DynArr(Coro *) waiting_recv;
} CoroChannel;

CoroChannel *coro_channel_init(CoroChannel *chan, size_t cap);
void coro_channel_deinit(CoroChannel *chan);

int coro_channel_send(CoroChannel *chan, void *msg);
void *coro_channel_recv(CoroChannel *chan);

int coro_channel_try_send(CoroChannel *chan, void *msg);
void *coro_channel_try_recv(CoroChannel *chan);

typedef struct {
    DynArr(Coro *) coros;
    size_t finished_coros;
} CoroGroup;

CoroGroup *coro_group_init(CoroGroup *grp);
void coro_group_deinit(CoroGroup *grp);

void coro_group_add(CoroGroup *grp, Coro *coro);
void coro_group_remove(CoroGroup *grp, Coro *coro);

void coro_group_await(CoroGroup *grp);

#endif
