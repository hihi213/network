#ifndef NETWORK_H
#define NETWORK_H

#include "common.h"
#include "message.h" // Message 구조체 송수신을 위해 포함

/* TCP Keepalive 상수 - 플랫폼별 조건부 정의 */
#ifndef TCP_KEEPALIVE
#define TCP_KEEPALIVE 0x10
#endif

#ifndef TCP_KEEPIDLE
#define TCP_KEEPIDLE 0x11
#endif

#ifndef TCP_KEEPINTVL
#ifdef __APPLE__
#define TCP_KEEPINTVL 0x101  // macOS
#else
#define TCP_KEEPINTVL 0x12   // Linux
#endif
#endif

#ifndef TCP_KEEPCNT
#ifdef __APPLE__
#define TCP_KEEPCNT 0x102    // macOS
#else
#define TCP_KEEPCNT 0x13     // Linux
#endif
#endif

/* SSL 핸드셰이크 상태 열거형 */
typedef enum ssl_handshake_state {
    SSL_HANDSHAKE_INIT,
    SSL_HANDSHAKE_WANT_READ,
    SSL_HANDSHAKE_WANT_WRITE,
    SSL_HANDSHAKE_COMPLETE,
    SSL_HANDSHAKE_ERROR
} ssl_handshake_state_t;

/* SSL 핸들러/매니저 구조체 */
typedef struct ssl_handler {
    SSL* ssl;
    SSL_CTX* ctx;
    int socket_fd;
    ssl_handshake_state_t handshake_state;
    time_t last_activity;
    bool is_server;
} ssl_handler_t;

typedef struct ssl_manager {
    SSL_CTX* ctx;
    char cert_file[256];
    char key_file[256];
    bool is_server;
} ssl_manager_t;

/* 소켓/SSL 초기화 및 정리 함수 */
int network_init_server_socket(int port);

/**
 * @brief 클라이언트 소켓을 초기화하고 서버에 연결합니다.
 * @param ip 서버 IP 주소
 * @param port 서버 포트 번호
 * @return 성공 시 소켓 파일 디스크립터, 실패 시 -1
 */
int network_init_client_socket(const char* ip, int port);

int network_init_ssl_manager(ssl_manager_t* manager, bool is_server, const char* cert_file, const char* key_file);
void network_cleanup_ssl_manager(ssl_manager_t* manager);

/* SSL 핸들러 관리 함수 */
ssl_handler_t* network_create_ssl_handler(ssl_manager_t* manager, int socket_fd);
void network_cleanup_ssl_handler(ssl_handler_t* handler);
int network_handle_ssl_handshake(ssl_handler_t* handler);
ssl_handler_t* network_accept_client(int server_fd, ssl_manager_t* ssl_manager, char* client_ip);

/**
 * @brief SSL 핸드셰이크를 수행합니다.
 * @param client_fd 클라이언트 소켓 파일 디스크립터
 * @param mgr SSL 매니저 포인터
 * @return 성공 시 ssl_handler_t 포인터, 실패 시 NULL
 */
ssl_handler_t* network_perform_ssl_handshake(int client_fd, ssl_manager_t* mgr);

/* 메시지 송수신 함수 */
int network_send_message(SSL* ssl, const message_t* message);

/**
 * @brief 소켓 옵션을 설정합니다. (서버/클라이언트 구분)
 * @param socket_fd 소켓 파일 디스크립터
 * @param is_server 서버 소켓 여부
 * @return 성공 시 0, 실패 시 -1
 */
int network_set_socket_options(int socket_fd, bool is_server);

void network_update_ssl_activity(ssl_handler_t* handler);

/**
 * @brief SSL을 통한 안전한 송신 함수 (타임아웃, 재시도, 에러 로깅 포함)
 * @param ssl SSL 객체
 * @param buf 송신 버퍼
 * @param len 송신 길이
 * @return 성공 시 송신 바이트 수, 실패 시 -1
 */
ssize_t network_send(SSL* ssl, const void* buf, size_t len);

/**
 * @brief SSL을 통한 안전한 수신 함수 (타임아웃, 재시도, 에러 로깅 포함)
 * @param ssl SSL 객체
 * @param buf 수신 버퍼
 * @param len 수신 길이
 * @return 성공 시 수신 바이트 수, 실패 시 -1
 */
ssize_t network_recv(SSL* ssl, void* buf, size_t len);

#endif /* NETWORK_H */ 