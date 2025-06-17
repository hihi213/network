#ifndef COMMON_H
#define COMMON_H

/* 표준 라이브러리 헤더 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

/* 시스템 관련 헤더 */
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/time.h>

/* 외부 라이브러리 헤더 */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <ncurses.h>
#include <menu.h>
#include <form.h>

#include <locale.h>
#include <time.h>
/* 전역 상수 정의 */
#define PORT 8080
#define MAX_BUFFER_SIZE 4096
#define MAX_INPUT_LENGTH 256
#define MAX_LOG_MSG 1024
#define MAX_USERNAME_LENGTH 32
#define MAX_PASSWORD_LENGTH 32
#define MAX_ID_LENGTH 32
#define MAX_DEVICE_NAME_LENGTH 64
#define MAX_DEVICE_TYPE_LENGTH 32
#define MAX_DEVICE_ID_LEN 32
#define MAX_DEVICE_STATUS_LENGTH 32
#define MAX_DEVICES 100
#define MAX_RESERVATIONS 1000
#define MAX_REASON_LEN 256
#define MAX_RESERVATIONS_PER_USER 10
#define MAX_CLIENTS 100
#define MAX_SESSIONS 100
#define SESSION_TIMEOUT 3600
#define MAX_IP_LENGTH 46
#define MAX_TOKEN_LENGTH 65
#define MAX_ARGS 10

/* 성능 최적화 매크로 */
#define CACHE_LINE_SIZE 64
#define ALIGN_CACHE __attribute__((aligned(CACHE_LINE_SIZE)))
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#endif /* COMMON_H */