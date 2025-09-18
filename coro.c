#include "coro.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <threads.h>
#include <ucontext.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include "dyn_arr.h"

thread_local static ucontext_t main_ctx;
thread_local static DynArr(Coro *) ready_coros = {0};
thread_local static DynArr(Coro *) finished_coros = {0};
thread_local static size_t sleeping_coros_count = 0;
thread_local static int epoll_fd;
thread_local static Coro *cur_coro = NULL;

void coro_destroy(Coro *coro) {
    free(coro->stack);
}

static void coro_trampoline(uintptr_t ptr) {
    Coro *coro = (Coro *) ptr;
    coro->state = CORO_RUNNING;

    coro->result = coro->entry.func(coro->entry.arg);

    coro->state = CORO_FINISHED;
    if (coro->detached) {
        darr_push(&finished_coros, coro);
    }

    if (coro->waiting_coro) {
        setcontext(&coro->waiting_coro->ctx);
    } else {
        setcontext(&main_ctx);
    }
}

static void coro_reset(Coro *coro, void *(*func)(void *), void *arg) {
    coro->waiting_events = 0;
    coro->waiting_fd = -1;
    coro->state = CORO_READY;
    coro->entry.func = func;
    coro->entry.arg = arg;
    coro->waiting_coro = NULL;
    coro->detached = 0;
    getcontext(&coro->ctx);
    coro->ctx.uc_stack.ss_sp = coro->stack;
    coro->ctx.uc_stack.ss_size = CORO_STACK_SIZE;
    coro->ctx.uc_link = &main_ctx;
    makecontext(&coro->ctx, (void (*)(void)) coro_trampoline, 1, (uintptr_t) coro);
}

Coro *coro_spawn(void *(*func)(void *), void *arg) {
    Coro *coro = NULL;

    if (finished_coros.size > 0) {
        coro = finished_coros.items[finished_coros.size - 1];
        darr_pop(&finished_coros);
    } else {
        coro = (Coro *) malloc(sizeof(Coro));
        coro->stack = malloc(CORO_STACK_SIZE);
    }

    coro_reset(coro, func, arg);
    darr_push(&ready_coros, coro);

    return coro;
}

void coro_yield(void) {
    if (!cur_coro) return;
    cur_coro->state = CORO_SUSPENDED;
    swapcontext(&cur_coro->ctx, &main_ctx);
}

void coro_sleep_fd(int fd, int events) {
    if (fd < 0) {
        coro_yield();
        return;
    }

    if (!cur_coro) return;

    Coro *coro = cur_coro;
    coro->state = CORO_SLEEPING;
    coro->waiting_fd = fd;
    coro->waiting_events = events;

    sleeping_coros_count += 1;

    struct epoll_event ev = {
        .events = events,
        .data.ptr = coro
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        if (errno == EEXIST) {
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
        } else {
            perror("epoll_ctl");
            exit(1);
        }
    }

    swapcontext(&coro->ctx, &main_ctx);
}

void coro_sleep_ms(int ms) {
    if (ms <= 0) {
        coro_yield();
        return;
    }

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

    struct itimerspec its = {0};
    its.it_value.tv_sec = ms / 1000;
    its.it_value.tv_nsec = (ms % 1000) * 1000000;

    if (timerfd_settime(tfd, 0, &its, NULL) < 0) {
        close(tfd);
        coro_yield();
        return;
    }

    coro_sleep_fd(tfd, EPOLLIN);
    int a;
    read(tfd, &a, 4);
    close(tfd);
}

void *coro_await(Coro *target) {
    Coro *caller = cur_coro;

    if (target->state == CORO_FINISHED) {
        darr_push(&finished_coros, target);
        return target->result;
    }

    if (caller) {
        target->waiting_coro = caller;
        caller->state = CORO_SLEEPING;

        swapcontext(&caller->ctx, &main_ctx);
        void *result = target->result;
        darr_push(&finished_coros, target);
        darr_push(&ready_coros, caller);
        caller->state = CORO_READY;
        swapcontext(&caller->ctx, &main_ctx);

        return result;
    }

    getcontext(&main_ctx);

    epoll_fd = epoll_create1(0);
    struct epoll_event events[64];

    while (target->state != CORO_FINISHED) {
        darr_foreach(Coro *, &ready_coros, coro) {
            cur_coro = *coro;

            cur_coro->state = CORO_RUNNING;
            swapcontext(&main_ctx, &cur_coro->ctx);

            if (cur_coro->state == CORO_FINISHED || cur_coro->state == CORO_SLEEPING) {
                *coro = ready_coros.items[ready_coros.size - 1];
                coro -= 1;
                ready_coros.size -= 1;
            }
        }


        if (!sleeping_coros_count) continue;
        int n = epoll_wait(epoll_fd, events, 64, ready_coros.size > 0 ? 0 : -1);

        for (int i = 0; i < n; ++i) {
            Coro *coro = (Coro *) events[i].data.ptr;
            if (events[i].events & coro->waiting_events) {
                coro->state = CORO_READY;
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, coro->waiting_fd, NULL);
                coro->waiting_fd = -1;
                coro->waiting_events = 0;
                sleeping_coros_count -= 1;
                darr_push(&ready_coros, coro);
            }
        }
    }

    cur_coro = NULL;
    void *result = target->result;

    close(epoll_fd);

    darr_foreach(Coro *, &ready_coros, coro) {
        if ((*coro)->state == CORO_FINISHED) continue;
        coro_destroy(*coro);
        free(*coro);
    }

    darr_foreach(Coro *, &finished_coros, coro) {
        coro_destroy(*coro);
        free(*coro);
    }

    darr_deinit(&ready_coros);
    darr_deinit(&finished_coros);

    return result;
}

void coro_detach(Coro *target) {
    target->detached = 1;
}

CoroChannel *coro_channel_init(CoroChannel *chan, size_t cap) {
    chan->buf = malloc(sizeof(void *) * cap);
    chan->cap = cap;
    chan->size = 0;
    chan->head = 0;
    chan->tail = 0;

    darr_init(&chan->waiting_send, NULL);
    darr_init(&chan->waiting_recv, NULL);

    return chan;
}

void coro_channel_deinit(CoroChannel *chan) {
    free(chan->buf);
    darr_deinit(&chan->waiting_send);
    darr_deinit(&chan->waiting_recv);
}

int coro_channel_try_send(CoroChannel *chan, void *msg) {
    if (chan->size == chan->cap) return 0;

    chan->buf[chan->tail] = msg;
    chan->tail = (chan->tail + 1) % chan->cap;
    chan->size += 1;

    if (chan->waiting_recv.size > 0) {
        Coro *receiver = chan->waiting_recv.items[0];
        receiver->state = CORO_READY;
        darr_remove(&chan->waiting_recv, 0);
        darr_push(&ready_coros, receiver);
    }

    return 1;
}

void *coro_channel_try_recv(CoroChannel *chan) {
    if (chan->size == 0) return NULL;
    void *msg = chan->buf[chan->head];
    chan->head = (chan->head + 1) % chan->cap;
    chan->size -= 1;

    if (chan->waiting_send.size > 0) {
        Coro *sender = chan->waiting_send.items[0];
        sender->state = CORO_READY;
        darr_remove(&chan->waiting_send, 0);
    }

    return msg;
}

int coro_channel_send(CoroChannel *chan, void *msg) {
    while (chan->size == chan->cap) {
        darr_push(&chan->waiting_send, cur_coro);
        cur_coro->state = CORO_SLEEPING;
        coro_yield();
    }

    return coro_channel_try_send(chan, msg);
}

void *coro_channel_recv(CoroChannel *chan) {
    while (chan->size == 0) {
        darr_push(&chan->waiting_recv, cur_coro);
        cur_coro->state = CORO_SLEEPING;
        coro_yield();
    }

    return coro_channel_try_recv(chan);
}

CoroGroup *coro_group_init(CoroGroup *grp) {
    darr_init(&grp->coros, NULL);
    grp->finished_coros = 0;

    // darr_foreach(Coro *, &grp->coros, coro) {
    //     if ((*coro)->state == CORO_FINISHED) {
    //         grp->finished_coros += 1;
    //     }
    // }

    return grp;
}

void coro_group_deinit(CoroGroup *grp) {
    darr_deinit(&grp->coros);
    grp->finished_coros = 0;
}

void coro_group_add(CoroGroup *grp, Coro *coro) {
    darr_push(&grp->coros, coro);
}

void coro_group_remove(CoroGroup *grp, Coro *coro) {
    darr_foreach(Coro *, &grp->coros, c) {
        if (coro == *c) {
            darr_remove(&grp->coros, c - grp->coros.items);
            return;
        }
    }
}

void coro_group_await(CoroGroup *grp) {
    darr_foreach(Coro *, &grp->coros, coro) {
        coro_await(*coro);
        grp->finished_coros += 1;
    }
}
