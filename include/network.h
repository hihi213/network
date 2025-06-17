#ifndef NETWORK_H
#define NETWORK_H

#include "common.h"
#include "message.h" // Message 구조체 송수신을 위해 포함

/* TCP Keepalive 상수 */
#define TCP_KEEPALIVE 0x10
#define TCP_KEEPIDLE 0x11
#define TCP_KEEPINTVL 0x12
#define TCP_KEEPCNT 0x13

/* SSL 핸드셰이크 상태 열거형 */
typedef enum {
    SSL_HANDSHAKE_INIT,
    SSL_HANDSHAKE_WANT_READ,
    SSL_HANDSHAKE_WANT_WRITE,
    SSL_HANDSHAKE_COMPLETE,
    SSL_HANDSHAKE_ERROR
} SSLHandshakeState;

/* SSL 핸들러/매니저 구조체 */
typedef struct {
    SSL* ssl;
    SSL_CTX* ctx;
    int socket_fd;
    SSLHandshakeState handshake_state;
    time_t last_activity;
    bool is_server;
} SSLHandler;

typedef struct {
    SSL_CTX* ctx;
    char cert_file[256];
    char key_file[256];
    bool is_server;
} SSLManager;

/* 소켓/SSL 초기화 및 정리 함수 */
int init_server_socket(int port);
int init_client_socket(const char* server_ip, int port);
int init_ssl_manager(SSLManager* manager, bool is_server, const char* cert_file, const char* key_file);
void cleanup_ssl_manager(SSLManager* manager);

/* SSL 핸들러 관리 함수 */
SSLHandler* create_ssl_handler(SSLManager* manager, int socket_fd);
void cleanup_ssl_handler(SSLHandler* handler);
int handle_ssl_handshake(SSLHandler* handler);

/* 메시지 송수신 함수 */
int send_message(SSL* ssl, const Message* message);

/* 유틸리티 함수 */
int set_nonblocking(int socket_fd);
bool is_connection_alive(SSL* ssl, int socket_fd);
int set_socket_options(int socket_fd);
void update_ssl_activity(SSLHandler* handler);

#endif /* NETWORK_H */ 