// network.c (최종 수정본)
/**
 * @file network.c
 * @brief 네트워크 통신 모듈 - SSL/TLS 기반의 안전한 소켓 통신 기능
 * @details 서버/클라이언트 소켓 초기화, SSL 핸드셰이크, 메시지 전송/수신을 담당합니다.
 */

#include "../include/network.h"
#include "../include/message.h"
#include "../include/utils.h" // 매크로 사용을 위해 추가
#include <netinet/tcp.h>  // TCP_NODELAY, TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT
#include <sys/socket.h>   // SO_REUSEADDR, SO_REUSEPORT, SO_KEEPALIVE, SO_RCVTIMEO, SO_SNDTIMEO
#include <netinet/in.h>   // sockaddr_in
#include <arpa/inet.h>    // inet_pton, inet_ntop
#include <unistd.h>       // close
#include <errno.h>        // errno
#include <string.h>       // strerror

#define NET_IO_MAX_RETRY 3

// [내부 함수] 요청한 길이만큼 데이터를 완전히 전송하는 안정적인 쓰기 함수
static bool network_ssl_write_fully(SSL* ssl, const void* buf, int len) {
    if (!ssl || !buf) return false;
    if (len == 0) return true;

    const char* ptr = (const char*)buf;
    while (len > 0) {
        int bytes_written = SSL_write(ssl, ptr, len);
        if (bytes_written <= 0) {
            int err = SSL_get_error(ssl, bytes_written);
            if (err != SSL_ERROR_ZERO_RETURN) {
                utils_report_error(ERROR_NETWORK_SEND_FAILED, "Network", "SSL_write 에러 발생: %d", err);
            }
            return false;
        }
        ptr += bytes_written;
        len -= bytes_written;
    }
    return true;
}

// [핵심 수정] 길이가 0인 문자열도 올바르게 처리하도록 수정
int network_send_message(SSL* ssl, const Message* message) {
    if (!ssl || !message) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Network", "network_send_message: 잘못된 파라미터");
        return -1;
    }

    // 1. 메시지 헤더 전송 (타입, 인자 개수)
    uint32_t net_type = htonl(message->type);
    if (network_send(ssl, &net_type, sizeof(net_type)) != sizeof(net_type)) return -1;

    uint32_t net_arg_count = htonl(message->arg_count);
    if (network_send(ssl, &net_arg_count, sizeof(net_arg_count)) != sizeof(net_arg_count)) return -1;

    // 2. 각 인자 전송 (길이 + 내용)
    for (int i = 0; i < message->arg_count; i++) {
        size_t arg_len = strlen(message->args[i]);
        uint32_t net_arg_len = htonl(arg_len);
        if (network_send(ssl, &net_arg_len, sizeof(net_arg_len)) != sizeof(net_arg_len)) return -1;
        // 길이가 0보다 클 때만 내용을 전송
        if (arg_len > 0) {
            if (network_send(ssl, message->args[i], arg_len) != (ssize_t)arg_len) return -1;
        }
    }

    // 3. 데이터 필드 전송 (길이 + 내용)
    size_t data_len = strlen(message->data);
    uint32_t net_data_len = htonl(data_len);
    if (network_send(ssl, &net_data_len, sizeof(net_data_len)) != sizeof(net_data_len)) return -1;
    // 길이가 0보다 클 때만 내용을 전송
    if (data_len > 0) {
        if (network_send(ssl, message->data, data_len) != (ssize_t)data_len) return -1;
    }

    // LOG_INFO("Network", "메시지 전송 완료: 타입=%s", message_get_type_string(message->type));
    return 0;
}

ssize_t network_send(SSL* ssl, const void* buf, size_t len) {
    int retry = 0;
    size_t total_sent = 0;
    while (total_sent < len && retry < NET_IO_MAX_RETRY) {
        int ret = SSL_write(ssl, (const char*)buf + total_sent, (int)(len - total_sent));
        if (ret > 0) {
            total_sent += ret;
            continue;
        }
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_SYSCALL) {
            utils_report_error(ERROR_NETWORK_SEND_FAILED, "Network", "network_send: 재시도 필요 (err=%d, retry=%d)", err, retry);
            retry++;
            continue;
        } else if (err == SSL_ERROR_ZERO_RETURN) {
            utils_report_error(ERROR_NETWORK_SEND_FAILED, "Network", "network_send: 연결 종료 감지");
            return 0;
        } else {
            utils_report_error(ERROR_NETWORK_SEND_FAILED, "Network", "network_send: 치명적 에러 (err=%d)", err);
            return -1;
        }
    }
    if (total_sent < len) {
        utils_report_error(ERROR_NETWORK_SEND_FAILED, "Network", "network_send: 송신 미완료 (total_sent=%zu, len=%zu)", total_sent, len);
        return -1;
    }
    return (ssize_t)total_sent;
}

ssize_t network_recv(SSL* ssl, void* buf, size_t len) {
    int retry = 0;
    size_t total_recv = 0;
    while (total_recv < len && retry < NET_IO_MAX_RETRY) {
        int ret = SSL_read(ssl, (char*)buf + total_recv, (int)(len - total_recv));
        if (ret > 0) {
            total_recv += ret;
            continue;
        }
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_SYSCALL) {
            utils_report_error(ERROR_NETWORK_RECEIVE_FAILED, "Network", "network_recv: 재시도 필요 (err=%d, retry=%d)", err, retry);
            retry++;
            continue;
        } else if (err == SSL_ERROR_ZERO_RETURN) {
            utils_report_error(ERROR_NETWORK_RECEIVE_FAILED, "Network", "network_recv: 연결 종료 감지");
            return 0;
        } else {
            utils_report_error(ERROR_NETWORK_RECEIVE_FAILED, "Network", "network_recv: 치명적 에러 (err=%d)", err);
            return -1;
        }
    }
    if (total_recv < len) {
        utils_report_error(ERROR_NETWORK_RECEIVE_FAILED, "Network", "network_recv: 수신 미완료 (total_recv=%zu, len=%zu)", total_recv, len);
        return -1;
    }
    return (ssize_t)total_recv;
}

// --- 이하 다른 함수들은 이전과 동일 (생략) ---

static void network_set_common_ssl_ctx_options(SSL_CTX* ctx) {
    if (!ctx) return;
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
    SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
}

static int network_init_ssl_context(SSLManager* manager, const char* cert_file, const char* key_file) {
    if (!manager || !cert_file || !key_file) {
        utils_report_error(ERROR_INVALID_PARAMETER, "SSL", "network_init_ssl_context: 잘못된 파라미터");
        return -1;
    }
    manager->ctx = SSL_CTX_new(TLS_server_method());
    if (!manager->ctx) {
        utils_report_error(ERROR_NETWORK_SSL_CONTEXT_FAILED, "SSL", "SSL_CTX_new 실패");
        return -1;
    }
    if (SSL_CTX_use_certificate_file(manager->ctx, cert_file, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(manager->ctx, key_file, SSL_FILETYPE_PEM) <= 0 ||
        !SSL_CTX_check_private_key(manager->ctx)) {
        utils_report_error(ERROR_NETWORK_SSL_CERTIFICATE_FAILED, "SSL", "인증서 또는 개인키 파일 로드/검증 실패");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(manager->ctx);
        manager->ctx = NULL;
        return -1;
    }
    network_set_common_ssl_ctx_options(manager->ctx);
    return 0;
}

int network_init_ssl_manager(SSLManager* manager, bool is_server, const char* cert_file, const char* key_file) {
    if (!manager) {
        utils_report_error(ERROR_INVALID_PARAMETER, "SSL", "manager 포인터가 NULL입니다");
        return -1;
    }
    memset(manager, 0, sizeof(SSLManager));
    manager->is_server = is_server;
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    SSL_library_init();

    if (is_server) {
        if (!cert_file || !key_file) {
            utils_report_error(ERROR_INVALID_PARAMETER, "SSL", "서버 모드에서는 인증서와 키 파일이 필요합니다");
            return -1;
        }
        return network_init_ssl_context(manager, cert_file, key_file);
    } else {
        manager->ctx = SSL_CTX_new(TLS_client_method());
        if (!manager->ctx) {
            utils_report_error(ERROR_NETWORK_SSL_CONTEXT_FAILED, "SSL", "클라이언트 SSL 컨텍스트 생성 실패");
            return -1;
        }
        network_set_common_ssl_ctx_options(manager->ctx);
        return 0;
    }
}

int network_init_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_SYSCALL_RET(server_fd, ERROR_NETWORK_SOCKET_CREATION_FAILED, "Network", "서버 소켓 생성 실패");
    network_set_socket_options(server_fd, true);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        utils_report_error(ERROR_NETWORK_BIND_FAILED, "Network", "서버 소켓 바인딩 실패: %s", strerror(errno));
        CLEANUP_AND_RET(close(server_fd), -1);
    }
    if (listen(server_fd, SOMAXCONN) < 0) {
        utils_report_error(ERROR_NETWORK_LISTEN_FAILED, "Network", "서버 소켓 리스닝 실패: %s", strerror(errno));
        CLEANUP_AND_RET(close(server_fd), -1);
    }
    return server_fd;
}

int network_init_client_socket(const char* server_ip, int port) {
    CHECK_PARAM_RET(server_ip, ERROR_INVALID_PARAMETER, "Network", "server_ip가 NULL입니다");
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_SYSCALL_RET(client_fd, ERROR_NETWORK_SOCKET_CREATION_FAILED, "Network", "클라이언트 소켓 생성 실패");
    if (network_set_socket_options(client_fd, false) < 0) {
        CLEANUP_AND_RET(close(client_fd), -1);
    }
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        utils_report_error(ERROR_NETWORK_IP_CONVERSION_FAILED, "Network", "IP 주소 변환 실패: %s", strerror(errno));
        CLEANUP_AND_RET(close(client_fd), -1);
    }
    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        utils_report_error(ERROR_NETWORK_CONNECT_FAILED, "Network", "서버 연결 실패: %s", strerror(errno));
        CLEANUP_AND_RET(close(client_fd), -1);
    }
    return client_fd;
}

int network_handle_ssl_handshake(SSLHandler* handler) {
    CHECK_PARAM_RET(handler && handler->ssl, ERROR_INVALID_PARAMETER, "SSL", "잘못된 SSL 핸들러");
    int ret = handler->is_server ? SSL_accept(handler->ssl) : SSL_connect(handler->ssl);
    if (ret <= 0) {
        utils_report_error(ERROR_NETWORK_SSL_HANDSHAKE_FAILED, "SSL", "SSL 핸드셰이크 실패");
        ERR_print_errors_fp(stderr);
        return -1;
    }
    return 0;
}

SSLHandler* network_create_ssl_handler(SSLManager* manager, int socket_fd) {
    CHECK_PARAM_RET_PTR(manager && manager->ctx, ERROR_INVALID_PARAMETER, "SSL", "잘못된 SSL Manager 또는 Context");
    SSLHandler* handler = (SSLHandler*)malloc(sizeof(SSLHandler));
    if (!handler) {
        utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "SSL", "SSL 핸들러 메모리 할당 실패");
        return NULL;
    }
    memset(handler, 0, sizeof(SSLHandler));
    handler->socket_fd = socket_fd;
    handler->ctx = manager->ctx;
    handler->is_server = manager->is_server;
    handler->ssl = SSL_new(manager->ctx);
    if (!handler->ssl) {
        utils_report_error(ERROR_NETWORK_SSL_CONTEXT_FAILED, "SSL", "SSL 객체 생성 실패");
        free(handler);
        return NULL;
    }
    if (!SSL_set_fd(handler->ssl, socket_fd)) {
        utils_report_error(ERROR_NETWORK_SSL_CONTEXT_FAILED, "SSL", "SSL 소켓 설정 실패");
        SSL_free(handler->ssl);
        free(handler);
        return NULL;
    }
    return handler;
}

void network_cleanup_ssl_handler(SSLHandler* handler) {
    if (!handler) return;
    if (handler->ssl) {
        SSL_shutdown(handler->ssl);
        SSL_free(handler->ssl);
    }
    free(handler);
}

int network_set_socket_options(int socket_fd, bool is_server) {
    int opt = 1;
    // SO_REUSEADDR: 서버/클라이언트 모두 적용
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "SO_REUSEADDR 설정 실패: %s", strerror(errno));
        return -1;
    }

    if (is_server) {
        // SO_REUSEPORT: 서버 소켓에만 적용 (macOS/Linux)
        #ifdef SO_REUSEPORT
        if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
            utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "SO_REUSEPORT 설정 실패: %s", strerror(errno));
            return -1;
        }
        #endif
    }

    // SO_KEEPALIVE: 서버/클라이언트 모두 적용
    if (setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
        utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "SO_KEEPALIVE 설정 실패: %s", strerror(errno));
        return -1;
    }

    // TCP_NODELAY: 서버/클라이언트 모두 적용
    #ifdef TCP_NODELAY
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "TCP_NODELAY 설정 실패: %s", strerror(errno));
        return -1;
    }
    #endif

    // 타임아웃: 서버/클라이언트 모두 적용
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "SO_RCVTIMEO 설정 실패: %s", strerror(errno));
        return -1;
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "SO_SNDTIMEO 설정 실패: %s", strerror(errno));
        return -1;
    }

    // TCP_KEEPALIVE 관련 옵션: 서버 소켓에만 적용, 플랫폼별 분기
    if (is_server) {
        #if defined(TCP_KEEPIDLE)
        int keepidle = 60;
        if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) < 0) {
            utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "TCP_KEEPIDLE 설정 실패: %s", strerror(errno));
            return -1;
        }
        #elif defined(TCP_KEEPALIVE) // macOS
        int keepidle = 60;
        if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPALIVE, &keepidle, sizeof(keepidle)) < 0) {
            utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "TCP_KEEPALIVE(macOS) 설정 실패: %s", strerror(errno));
            return -1;
        }
        #endif
        #ifdef TCP_KEEPINTVL
        int keepintvl = 10;
        if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl)) < 0) {
            utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "TCP_KEEPINTVL 설정 실패: %s", strerror(errno));
            return -1;
        }
        #endif
        #ifdef TCP_KEEPCNT
        int keepcnt = 3;
        if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt)) < 0) {
            utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "TCP_KEEPCNT 설정 실패: %s", strerror(errno));
            return -1;
        }
        #endif
    }

    // LOG_INFO("Network", "소켓 옵션 설정 완료: fd=%d, is_server=%d", socket_fd, is_server);
    return 0;
}

void network_cleanup_ssl_manager(SSLManager* manager) {
    if (!manager) return;
    if (manager->ctx) {
        SSL_CTX_free(manager->ctx);
    }
    EVP_cleanup();
    ERR_free_strings();
}

/**
 * @brief 클라이언트 연결을 수락하고 SSL 핸드셰이크를 수행합니다.
 * @param server_fd 서버 소켓 파일 디스크립터
 * @param ssl_manager SSL 매니저 포인터
 * @param client_ip 클라이언트 IP 주소를 저장할 버퍼
 * @return 성공 시 SSLHandler* 포인터, 실패 시 NULL
 */
SSLHandler* network_accept_client(int server_fd, SSLManager* ssl_manager, char* client_ip) {
    CHECK_PARAM_RET_PTR(ssl_manager && client_ip, ERROR_INVALID_PARAMETER, "Network", "network_accept_client: 잘못된 파라미터");
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        utils_report_error(ERROR_NETWORK_ACCEPT_FAILED, "Network", "클라이언트 연결 수락 실패: %s", strerror(errno));
        return NULL;
    }
    if (network_set_socket_options(client_fd, false) < 0) {
        utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "클라이언트 소켓 옵션 설정 실패");
        CLEANUP_AND_RET(close(client_fd), NULL);
    }
    if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN) == NULL) {
        utils_report_error(ERROR_NETWORK_IP_CONVERSION_FAILED, "Network", "클라이언트 IP 주소 변환 실패");
        CLEANUP_AND_RET(close(client_fd), NULL);
    }
    // LOG_INFO("Network", "클라이언트 연결 수락: IP=%s, 소켓=%d", client_ip, client_fd);
    SSLHandler* ssl_handler = network_create_ssl_handler(ssl_manager, client_fd);
    if (!ssl_handler) {
        utils_report_error(ERROR_NETWORK_SSL_INIT_FAILED, "Network", "SSL 핸들러 생성 실패: IP=%s", client_ip);
        CLEANUP_AND_RET(close(client_fd), NULL);
    }
    if (network_handle_ssl_handshake(ssl_handler) != 0) {
        utils_report_error(ERROR_NETWORK_SSL_HANDSHAKE_FAILED, "Network", "SSL 핸드셰이크 실패: IP=%s", client_ip);
        network_cleanup_ssl_handler(ssl_handler);
        CLEANUP_AND_RET(close(client_fd), NULL);
    }
    // LOG_INFO("Network", "클라이언트 SSL 연결 성공: IP=%s", client_ip);
    return ssl_handler;
}

/**
 * @brief SSL 핸드셰이크를 수행합니다.
 * @param client_fd 클라이언트 소켓 파일 디스크립터
 * @param mgr SSL 매니저 포인터
 * @return 성공 시 SSLHandler 포인터, 실패 시 NULL
 */
SSLHandler* network_perform_ssl_handshake(int client_fd, SSLManager* mgr) {
    CHECK_PARAM_RET_PTR(mgr, ERROR_INVALID_PARAMETER, "Network", "network_perform_ssl_handshake: SSL 매니저가 NULL입니다");
    // LOG_INFO("Network", "SSL 핸드셰이크 시작: fd=%d", client_fd);
    SSLHandler* handler = network_create_ssl_handler(mgr, client_fd);
    if (!handler) {
        utils_report_error(ERROR_NETWORK_SSL_INIT_FAILED, "Network", "SSL 핸들러 생성 실패: fd=%d", client_fd);
        close(client_fd);
        return NULL;
    }
    if (network_handle_ssl_handshake(handler) != 0) {
        utils_report_error(ERROR_NETWORK_SSL_HANDSHAKE_FAILED, "Network", "SSL 핸드셰이크 실패: fd=%d", client_fd);
        network_cleanup_ssl_handler(handler);
        close(client_fd);
        return NULL;
    }
    // LOG_INFO("Network", "SSL 핸드셰이크 성공: fd=%d", client_fd);
    return handler;
}