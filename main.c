#include "coro.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>

void *receiver(void *arg) {
    CoroChannel *chan = arg;

    while (1) {
        char *msg = coro_channel_recv(chan);
        if (msg == NULL) return NULL;
        printf("Got: %s\n", msg);
    }

    return NULL;
}

void *sender(void *arg) {
    CoroChannel *chan = arg;

    for (int i = 0; i < 10; ++i) {
        coro_channel_send(chan, "MSG");
        coro_sleep_ms(1000);
    }

    coro_channel_send(chan, NULL);

    return NULL;
}

void *main_coro(void *arg) {
    (void) arg;

    CoroChannel chan;
    coro_channel_init(&chan, 64);

    CoroGroup grp;
    coro_group_init(&grp);
    coro_group_add(&grp, coro_spawn(sender, &chan));
    coro_group_add(&grp, coro_spawn(receiver, &chan));

    coro_group_await(&grp);
    coro_channel_deinit(&chan);
    coro_group_deinit(&grp);

    return 0;
}

int main() {
    return (int)(intptr_t) coro_await(coro_spawn(main_coro, NULL));
}
