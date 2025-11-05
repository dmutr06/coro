# Sleep with Timeout Feature

## Overview

The `coro_sleep_fd_timeout()` function enables waiting on a file descriptor with a timeout. This is useful for I/O operations where you want to give up after a certain time period.

## Function Signature

```c
int coro_sleep_fd_timeout(int fd, int events, int timeout_ms);
```

### Parameters

- `fd`: File descriptor to wait on
- `events`: Events to wait for (e.g., `EPOLLIN`, `EPOLLOUT`)
- `timeout_ms`: Timeout in milliseconds
  - Positive value: Wait up to that many milliseconds
  - Zero: Don't wait - immediately check if fd is ready
  - Negative: Wait indefinitely (equivalent to `coro_sleep_fd()`)

### Return Value

- `1` if the file descriptor became ready before timeout
- `0` if timeout occurred

### Usage Example

```c
#include "coro.h"
#include <sys/epoll.h>

void *reader(void *arg) {
    int sockfd = *(int*)arg;
    
    // Wait up to 5 seconds for data
    int result = coro_sleep_fd_timeout(sockfd, EPOLLIN, 5000);
    
    if (result == 1) {
        // Data is available, read it
        char buf[1024];
        ssize_t n = read(sockfd, buf, sizeof(buf));
        // Process data...
    } else {
        // Timeout occurred
        printf("Timeout waiting for data\n");
    }
    
    return NULL;
}
```

## Implementation Details

- Uses Linux `timerfd` and `epoll` for efficient timeout handling
- Integrates seamlessly with the coroutine scheduler
- Properly cleans up resources (timer fd) regardless of which condition triggers first
- Prioritizes main fd readiness over timeout when both occur simultaneously

## Edge Cases

- **Invalid fd** (`fd < 0`): Yields and returns 0
- **Zero timeout**: Uses `poll()` to immediately check fd status without blocking
- **Negative timeout**: Equivalent to `coro_sleep_fd()` - waits indefinitely
- **Both fd and timeout ready**: Returns 1 (prioritizes fd ready)

## Testing

See `test_timeout.c` for comprehensive test coverage including:
- Timeout scenarios
- Immediate fd readiness
- Delayed fd readiness within timeout window
- Zero timeout edge cases
