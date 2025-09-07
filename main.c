#include "coro.h"
#include <stdio.h>
#include <sys/epoll.h>
#include <ucontext.h>
#include <unistd.h>

typedef struct {
    char *name;
    int count;
} WorkerCtx;

int count(char *from, int n) {
    int sum = 0;
    for (int i = 0; i < n; ++i) {
        sum += i;
        coro_yield();
    }

    return sum;
}

void worker(void *arg) {
    WorkerCtx *ctx = arg;

    int sum = count(ctx->name, ctx->count);

    printf("sum %s = %d\n", ctx->name, sum);
}

void reader(void *arg) {
    (void) arg;
    char buf[128];

    printf("Reader waiting for input...\n");
    fflush(stdout);

    coro_sleep_io(STDIN_FILENO, EPOLLIN);

    int n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("Reader got: %s\n", buf);
    }
}

int main() {
    WorkerCtx a = { "A", 10 };
    WorkerCtx b = { "B", 5 };
    WorkerCtx c = { "C", 7 };

    coro_create(reader, NULL);
    coro_create(worker, &a);
    coro_create(worker, &b);
    coro_create(worker, &c);

    coro_start();

    printf("This shit works\n");
    return 0;
}

