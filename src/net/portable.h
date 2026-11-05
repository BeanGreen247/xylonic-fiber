// Socket portability shims: Apple has no MSG_NOSIGNAL (SIGPIPE is ignored process-wide in main instead).
#pragma once
#include <sys/socket.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
