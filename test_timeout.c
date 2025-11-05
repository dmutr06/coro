#define _GNU_SOURCE
#include "coro.h"
#include <stdio.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

void *test_timeout_occurs(void *arg) {
    (void)arg;
    
    // Create a pipe that won't receive data (timeout should occur)
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        printf("✗ Failed to create pipe\n");
        return NULL;
    }
    
    // Set read end to non-blocking
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    
    printf("Testing timeout scenario...\n");
    int result = coro_sleep_fd_timeout(pipefd[0], EPOLLIN, 1000); // 1 second timeout
    
    if (result == 0) {
        printf("✓ Timeout occurred as expected\n");
    } else {
        printf("✗ Expected timeout but fd became ready\n");
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
    return NULL;
}

void *test_fd_ready(void *arg) {
    (void)arg;
    
    // Create a pipe where we immediately write data
    int pipefd[2];
    if (pipe2(pipefd, O_NONBLOCK) < 0) {
        printf("✗ Failed to create pipe\n");
        return NULL;
    }
    
    // Write some data to make the read end immediately readable
    write(pipefd[1], "test", 4);
    
    printf("Testing fd ready before timeout...\n");
    int result = coro_sleep_fd_timeout(pipefd[0], EPOLLIN, 5000); // 5 second timeout
    
    if (result == 1) {
        printf("✓ FD became ready before timeout\n");
        char buf[5] = {0};
        ssize_t n = read(pipefd[0], buf, 4);
        if (n < 0) {
            perror("read");
        }
    } else {
        printf("✗ Unexpected timeout\n");
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
    return NULL;
}

typedef struct {
    int write_fd;
} WriterArg;

void *delayed_writer(void *arg) {
    WriterArg *writer_arg = (WriterArg *)arg;
    coro_sleep_ms(500); // Sleep 500ms
    write(writer_arg->write_fd, "data", 4);
    return NULL;
}

void *test_delayed_fd_ready(void *arg) {
    (void)arg;
    
    // Create a pipe
    int pipefd[2];
    if (pipe2(pipefd, O_NONBLOCK) < 0) {
        printf("✗ Failed to create pipe\n");
        return NULL;
    }
    
    // Spawn a coroutine that writes after a delay
    WriterArg writer_arg = { .write_fd = pipefd[1] };
    Coro *writer = coro_spawn(delayed_writer, &writer_arg);
    coro_detach(writer);
    
    printf("Testing fd ready within timeout window...\n");
    int result = coro_sleep_fd_timeout(pipefd[0], EPOLLIN, 2000); // 2 second timeout
    
    if (result == 1) {
        printf("✓ FD became ready within timeout\n");
        char buf[5] = {0};
        ssize_t n = read(pipefd[0], buf, 4);
        if (n < 0) {
            perror("read");
        }
    } else {
        printf("✗ Timeout occurred when fd should have been ready\n");
    }
    
    coro_sleep_ms(100); // Give writer time to finish
    close(pipefd[0]);
    close(pipefd[1]);
    return NULL;
}

void *test_zero_timeout(void *arg) {
    (void)arg;
    
    // Create a pipe with data already available
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        printf("✗ Failed to create pipe\n");
        return NULL;
    }
    
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    write(pipefd[1], "test", 4);
    
    printf("Testing zero timeout with ready fd...\n");
    int result = coro_sleep_fd_timeout(pipefd[0], EPOLLIN, 0);
    
    if (result == 1) {
        printf("✓ Zero timeout correctly detected ready fd\n");
    } else {
        printf("✗ Zero timeout failed to detect ready fd\n");
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
    
    // Test with non-ready fd
    if (pipe(pipefd) < 0) {
        printf("✗ Failed to create pipe\n");
        return NULL;
    }
    
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    
    printf("Testing zero timeout with non-ready fd...\n");
    result = coro_sleep_fd_timeout(pipefd[0], EPOLLIN, 0);
    
    if (result == 0) {
        printf("✓ Zero timeout correctly detected non-ready fd\n");
    } else {
        printf("✗ Zero timeout incorrectly reported ready fd\n");
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
    
    return NULL;
}

void *main_coro(void *arg) {
    (void)arg;
    
    printf("=== Testing coro_sleep_fd_timeout ===\n\n");
    
    // Test 1: Timeout occurs
    coro_await(coro_spawn(test_timeout_occurs, NULL));
    printf("\n");
    
    // Test 2: FD ready immediately
    coro_await(coro_spawn(test_fd_ready, NULL));
    printf("\n");
    
    // Test 3: FD becomes ready before timeout
    coro_await(coro_spawn(test_delayed_fd_ready, NULL));
    printf("\n");
    
    // Test 4: Zero timeout
    coro_await(coro_spawn(test_zero_timeout, NULL));
    printf("\n");
    
    printf("=== All tests completed ===\n");
    
    return NULL;
}

int main() {
    coro_await(coro_spawn(main_coro, NULL));
    return 0;
}
