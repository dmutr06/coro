#include "coro.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ucontext.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define MAX_COROS 1024

static ucontext_t main_ctx;
static Coro coros[MAX_COROS] = {0};
static int count = 0;
static int cur = -1;
static int epoll_fd;

static int find_free_slot(void) {
    for (int i = 0; i < count ; i++) {
        if (coros[i].state == CORO_FINISHED) {
            printf("Reusing %d\n", i); 
            return i;
        }
    }

    return -1;
}

void coro_destroy(Coro *coro) {
    free(coro->stack);
}

void coro_trampoline(uintptr_t ptr) {
    int id = (int) ptr;
    Coro *coro = &coros[id];
    coro->state = CORO_RUNNING;

    coro->entry.func(coro->entry.arg);

    coro->state = CORO_FINISHED;

    setcontext(&main_ctx);
}

int coro_create(void (*func)(void *), void *arg) {
    int id = find_free_slot();

    if (id < 0) {
        id = count++;
        coros[id].stack = malloc(CORO_STACK_SIZE);
    }

    Coro *coro = &coros[id];

    coro->entry.func = func;
    coro->entry.arg = arg;
    coro->state = CORO_READY;
    coro->waiting_fd = -1;

    getcontext(&coro->ctx);
    coro->ctx.uc_stack.ss_sp = coro->stack;
    coro->ctx.uc_stack.ss_size = CORO_STACK_SIZE;
    coro->ctx.uc_link = &main_ctx;
    makecontext(&coro->ctx, (void(*)(void)) coro_trampoline, 1, id);

    return id;
}


void coro_yield(void) {
    Coro *coro = &coros[cur];
    coro->state = CORO_SUSPENDED;
    swapcontext(&coro->ctx, &main_ctx);
}

void coro_sleep_fd(int fd, int events) {
    if (fd < 0) {
        coro_yield();
        return;
    }

    Coro *coro = &coros[cur];
    coro->state = CORO_SLEEPING;
    coro->waiting_fd = fd;
    coro->waiting_events = events;

    struct epoll_event ev = {
        .events = events,
        .data.u32 = cur
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
    close(tfd);
}

void coro_start(void) {
    getcontext(&main_ctx);

    epoll_fd = epoll_create1(0);
    struct epoll_event events[64];

    while (1) {
        int active = 0;
        int ready_count = 0;
        int sleeping_count = 0;

        for (int i = 0; i < count; ++i) {
            if (coros[i].state == CORO_READY || coros[i].state == CORO_SUSPENDED) {
                ready_count += 1;
                cur = i;
                coros[i].state = CORO_RUNNING;
                swapcontext(&main_ctx, &coros[i].ctx);
            } else if (coros[i].state == CORO_SLEEPING) {
                sleeping_count += 1;
            } 

            if (coros[i].state != CORO_FINISHED) {
                active += 1;
            }
        }

        if (active == 0) break;

        if (sleeping_count == 0) continue;

        int n = epoll_wait(epoll_fd, events, 64, ready_count > 0 ? 0 : -1);
        for (int i = 0; i < n; ++i) {
            int id = events[i].data.u32;
            Coro *coro = &coros[id];
            coro->state = CORO_READY;
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, coro->waiting_fd, NULL);
            coro->waiting_fd = -1;
        }
    }

    close(epoll_fd);

    for (int i = 0; i < count; ++i) {
        coro_destroy(&coros[i]);
    }
}
