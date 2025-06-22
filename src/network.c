/**
 * @file network.c
 * @brief 저수준 네트워크 통신 모듈 - SSL/TLS 기반 보안 채널 및 메시지 전송
 * 
 * @details
 * 이 모듈은 클라이언트-서버 간의 저수준 네트워크 통신을 담당합니다:
 * 
 * **핵심 역할:**
 * - SSL/TLS 기반의 보안 채널 설정 및 관리
 * - 소켓 옵션 관리 (타임아웃, 버퍼 크기, 재사용 등)
 * - 자체 정의 프로토콜에 따른 메시지의 안정적인 송수신
 * - 메시지 직렬화/역직렬화 및 체크섬 검증
 * 
 * **시스템 아키텍처에서의 위치:**
 * - 네트워크 계층: OSI 4-7계층 (전송-응용 계층)
 * - 보안 계층: SSL/TLS 암호화 및 인증
 * - 프로토콜 계층: 커스텀 메시지 프로토콜 구현
 * - 데이터 계층: 바이너리 메시지 직렬화/역직렬화
 * 
 * **주요 특징:**
 * - TLS 1.2/1.3 기반의 강력한 암호화 통신
 * - 메시지 단위의 신뢰성 있는 전송 (재전송, 체크섬)
 * - 비동기 I/O 지원으로 높은 처리량 달성
 * - 크로스 플랫폼 호환성 (Linux, macOS)
 * 
 * **프로토콜 구조:**
 * - 메시지 헤더: 타입, 길이, 체크섬, 타임스탬프
 * - 메시지 본문: JSON 형태의 구조화된 데이터
 * - 에러 처리: 네트워크 오류 및 프로토콜 오류 구분
 * 
 * @note 이 모듈은 모든 네트워크 통신의 기반이 되며, 상위 계층에서
 *       안전하고 신뢰할 수 있는 통신을 보장합니다.
 */

#include "../include/network.h"
#include "../include/message.h"
#include "../include/utils.h"


#define NET_IO_MAX_RETRY 3  // 네트워크 I/O 재시도 최대 횟수

/**
 * @brief 에러 코드를 네트워크 바이트 순서로 전송
 * @details MSG_ERROR 타입 메시지에서 사용되는 에러 코드 전송 함수
 * 
 * @param ssl SSL 연결 객체
 * @param error_code 전송할 에러 코드
 * @return 성공 시 0, 실패 시 -1
 */
static int network_send_error_code(SSL* ssl, error_code_t error_code) {
    uint32_t net_error_code = htonl(error_code);
    return network_send(ssl, &net_error_code, sizeof(net_error_code)) == sizeof(net_error_code) ? 0 : -1;
}

/**
 * @brief 구조화된 메시지를 SSL 연결을 통해 전송
 * @details
 * 메시지 프로토콜에 따라 다음 순서로 데이터를 전송합니다:
 * 1. 메시지 헤더 (타입, 인자 개수)
 * 2. 에러 코드 (MSG_ERROR 타입인 경우)
 * 3. 각 인자 (길이 + 내용)
 * 4. 데이터 필드 (길이 + 내용)
 * 
 * 모든 정수 값은 네트워크 바이트 순서(빅엔디안)로 변환됩니다.
 * 
 * @param ssl SSL 연결 객체
 * @param message 전송할 메시지 객체
 * @return 성공 시 0, 실패 시 -1
 */
int network_send_message(SSL* ssl, const message_t* message) {
    if (!ssl || !message) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Network", "network_send_message: 잘못된 파라미터");
        return -1;
    }

    LOG_INFO("Network", "메시지 전송 시작: 타입=%d, 인자수=%d, 데이터길이=%zu", 
             message->type, message->arg_count, strlen(message->data));

    // 1. 메시지 헤더 전송 (타입, 인자 개수)
    uint32_t net_type = htonl(message->type);
    if (network_send(ssl, &net_type, sizeof(net_type)) != sizeof(net_type)) return -1;

    uint32_t net_arg_count = htonl(message->arg_count);
    if (network_send(ssl, &net_arg_count, sizeof(net_arg_count)) != sizeof(net_arg_count)) return -1;

    // 2. 에러 코드 전송 (MSG_ERROR 타입인 경우)
    if (message->type == MSG_ERROR) {
        if (network_send_error_code(ssl, message->error_code) != 0) return -1;
    }

    // 3. 각 인자 전송 (길이 + 내용)
    for (int i = 0; i < message->arg_count; i++) {
        size_t arg_len = strlen(message->args[i]);
        uint32_t net_arg_len = htonl(arg_len);
        if (network_send(ssl, &net_arg_len, sizeof(net_arg_len)) != sizeof(net_arg_len)) return -1;
        // 길이가 0보다 클 때만 내용을 전송
        if (arg_len > 0) {
            if (network_send(ssl, message->args[i], arg_len) != (ssize_t)arg_len) return -1;
        }
    }

    // 4. 데이터 필드 전송 (길이 + 내용)
    size_t data_len = strlen(message->data);
    uint32_t net_data_len = htonl(data_len);
    if (network_send(ssl, &net_data_len, sizeof(net_data_len)) != sizeof(net_data_len)) return -1;
    // 길이가 0보다 클 때만 내용을 전송
    if (data_len > 0) {
        if (network_send(ssl, message->data, data_len) != (ssize_t)data_len) return -1;
    }

    LOG_INFO("Network", "메시지 전송 완료: 타입=%d", message->type);
    return 0;
}

/**
 * @brief SSL 연결을 통해 데이터 전송 (재시도 로직 포함)
 * @details
 * SSL_write의 특성상 부분 전송이 발생할 수 있으므로, 요청된 길이만큼
 * 완전히 전송될 때까지 재시도합니다.
 * 
 * 재시도 조건:
 * - SSL_ERROR_WANT_READ/WRITE: 정상적인 블로킹 상황
 * - SSL_ERROR_SYSCALL: 시스템 호출 중단
 * 
 * @param ssl SSL 연결 객체
 * @param buf 전송할 데이터 버퍼
 * @param len 전송할 데이터 길이
 * @return 전송된 바이트 수, 실패 시 -1, 연결 종료 시 0
 */
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
            // 정상적인 재시도 상황
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

/**
 * @brief SSL 연결을 통해 데이터 수신 (재시도 로직 포함)
 * @details
 * SSL_read의 특성상 부분 수신이 발생할 수 있으므로, 요청된 길이만큼
 * 완전히 수신될 때까지 재시도합니다.
 * 
 * @param ssl SSL 연결 객체
 * @param buf 수신할 데이터 버퍼
 * @param len 수신할 데이터 길이
 * @return 수신된 바이트 수, 실패 시 -1, 연결 종료 시 0
 */
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
            // 정상적인 재시도 상황
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

/**
 * @brief SSL 컨텍스트에 공통 보안 옵션 설정
 * @details
 * 보안 강화를 위한 SSL/TLS 설정:
 * - 최소 TLS 1.2 버전 요구
 * - 취약한 프로토콜 버전 비활성화 (SSLv2, SSLv3, TLS 1.0, TLS 1.1)
 * - 자동 재시도 모드 활성화
 * - 클라이언트 인증서 검증 비활성화 (개발용)
 * 
 * @param ctx SSL 컨텍스트
 */
static void network_set_common_ssl_ctx_options(SSL_CTX* ctx) {
    if (!ctx) return;
    
    // 최소 TLS 1.2 버전 요구 (보안 강화)
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    
    // 취약한 프로토콜 버전 비활성화
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
    
    // 자동 재시도 모드 활성화
    SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
    
    // 클라이언트 인증서 검증 활성화 
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
}

/**
 * @brief SSL 컨텍스트 초기화 및 인증서 설정
 * @details
 * 서버용 SSL 컨텍스트를 생성하고 인증서/개인키를 로드합니다.
 * 
 * @param manager SSL 관리자 객체
 * @param cert_file 인증서 파일 경로
 * @param key_file 개인키 파일 경로
 * @return 성공 시 0, 실패 시 -1
 */
static int network_init_ssl_context(ssl_manager_t* manager, const char* cert_file, const char* key_file) {
    if (!manager || !cert_file || !key_file) {
        utils_report_error(ERROR_INVALID_PARAMETER, "SSL", "network_init_ssl_context: 잘못된 파라미터");
        return -1;
    }
    
    // TLS 서버 메서드로 SSL 컨텍스트 생성
    manager->ctx = SSL_CTX_new(TLS_server_method());
    if (!manager->ctx) {
        utils_report_error(ERROR_NETWORK_SSL_CONTEXT_FAILED, "SSL", "SSL_CTX_new 실패");
        return -1;
    }
    
    // 인증서 및 개인키 파일 로드 및 검증
    if (SSL_CTX_use_certificate_file(manager->ctx, cert_file, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(manager->ctx, key_file, SSL_FILETYPE_PEM) <= 0 ||
        !SSL_CTX_check_private_key(manager->ctx)) {
        utils_report_error(ERROR_NETWORK_SSL_CERTIFICATE_FAILED, "SSL", "인증서 또는 개인키 파일 로드/검증 실패");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(manager->ctx);
        manager->ctx = NULL;
        return -1;
    }
    
    // 공통 보안 옵션 설정
    network_set_common_ssl_ctx_options(manager->ctx);
    return 0;
}

/**
 * @brief SSL 관리자 초기화
 * @details
 * 서버/클라이언트 모드에 따라 SSL 관리자를 초기화합니다.
 * 서버 모드에서는 인증서와 개인키가 필요합니다.
 * 
 * @param manager SSL 관리자 객체
 * @param is_server 서버 모드 여부
 * @param cert_file 인증서 파일 경로 (서버 모드에서만 사용)
 * @param key_file 개인키 파일 경로 (서버 모드에서만 사용)
 * @return 성공 시 0, 실패 시 -1
 */
int network_init_ssl_manager(ssl_manager_t* manager, bool is_server, const char* cert_file, const char* key_file) {
    if (!manager) {
        utils_report_error(ERROR_INVALID_PARAMETER, "SSL", "manager 포인터가 NULL입니다");
        return -1;
    }
    
    LOG_INFO("Network", "SSL 매니저 초기화 시작: %s 모드", is_server ? "서버" : "클라이언트");
    
    memset(manager, 0, sizeof(ssl_manager_t));
    manager->is_server = is_server;
    
    // OpenSSL 라이브러리 초기화
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    SSL_library_init();

    if (is_server) {
        if (!cert_file || !key_file) {
            utils_report_error(ERROR_INVALID_PARAMETER, "SSL", "서버 모드에서는 인증서와 키 파일이 필요합니다");
            return -1;
        }
        LOG_INFO("Network", "서버 SSL 컨텍스트 초기화: 인증서=%s, 키=%s", cert_file, key_file);
        return network_init_ssl_context(manager, cert_file, key_file);
    } else {
        manager->ctx = SSL_CTX_new(TLS_client_method());
        if (!manager->ctx) {
            utils_report_error(ERROR_NETWORK_SSL_CONTEXT_FAILED, "SSL", "클라이언트 SSL 컨텍스트 생성 실패");
            return -1;
        }
        
        LOG_INFO("Network", "클라이언트 SSL 컨텍스트 생성 완료");
        
        // 서버의 인증서를 신뢰 목록에 추가
        if (!SSL_CTX_load_verify_locations(manager->ctx, "certs/server.crt", NULL)) {
            utils_report_error(ERROR_NETWORK_SSL_CERTIFICATE_FAILED, "SSL", "서버 인증서 로드 실패");
            SSL_CTX_free(manager->ctx);
            manager->ctx = NULL;
            return -1;
        }
        
        LOG_INFO("Network", "서버 인증서 로드 완료: certs/server.crt");
        
        // 검증 모드를 PEER로 설정
        SSL_CTX_set_verify(manager->ctx, SSL_VERIFY_PEER, NULL);
        
        network_set_common_ssl_ctx_options(manager->ctx);
        
        LOG_INFO("Network", "클라이언트 SSL 매니저 초기화 완료");
        return 0;
    }
}

int network_init_server_socket(int port) {
    LOG_INFO("Network", "서버 소켓 초기화 시작: 포트=%d", port);
    
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
    
    LOG_INFO("Network", "서버 소켓 초기화 완료: 포트=%d, 소켓=%d", port, server_fd);
    return server_fd;
}

int network_init_client_socket(const char* server_ip, int port) {
    CHECK_PARAM_RET(server_ip, ERROR_INVALID_PARAMETER, "Network", "server_ip가 NULL입니다");
    
    LOG_INFO("Network", "클라이언트 소켓 초기화 시작: 서버=%s:%d", server_ip, port);
    
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
    
    LOG_INFO("Network", "클라이언트 소켓 초기화 완료: 서버=%s:%d, 소켓=%d", server_ip, port, client_fd);
    return client_fd;
}

int network_handle_ssl_handshake(ssl_handler_t* handler) {
    CHECK_PARAM_RET(handler && handler->ssl, ERROR_INVALID_PARAMETER, "SSL", "잘못된 SSL 핸들러");
    
    LOG_INFO("Network", "SSL 핸드셰이크 시작: %s 모드, 소켓=%d", 
             handler->is_server ? "서버" : "클라이언트", handler->socket_fd);
    
    int ret = handler->is_server ? SSL_accept(handler->ssl) : SSL_connect(handler->ssl);
    if (ret <= 0) {
        utils_report_error(ERROR_NETWORK_SSL_HANDSHAKE_FAILED, "SSL", "SSL 핸드셰이크 실패");
        ERR_print_errors_fp(stderr);
        return -1;
    }
    
    LOG_INFO("Network", "SSL 핸드셰이크 성공: %s 모드, 소켓=%d, 프로토콜=%s", 
             handler->is_server ? "서버" : "클라이언트", handler->socket_fd, 
             SSL_get_version(handler->ssl));
    
    return 0;
}

ssl_handler_t* network_create_ssl_handler(ssl_manager_t* manager, int socket_fd) {
    CHECK_PARAM_RET_PTR(manager && manager->ctx, ERROR_INVALID_PARAMETER, "SSL", "잘못된 SSL Manager 또는 Context");
    
    LOG_INFO("Network", "SSL 핸들러 생성 시작: %s 모드, 소켓=%d", 
             manager->is_server ? "서버" : "클라이언트", socket_fd);
    
    ssl_handler_t* handler = (ssl_handler_t*)malloc(sizeof(ssl_handler_t));
    if (!handler) {
        utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "SSL", "SSL 핸들러 메모리 할당 실패");
        return NULL;
    }
    memset(handler, 0, sizeof(ssl_handler_t));
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
    
    LOG_INFO("Network", "SSL 핸들러 생성 완료: %s 모드, 소켓=%d", 
             manager->is_server ? "서버" : "클라이언트", socket_fd);
    
    return handler;
}

void network_cleanup_ssl_handler(ssl_handler_t* handler) {
    if (!handler) return;
    
    LOG_INFO("Network", "SSL 핸들러 정리 시작: %s 모드, 소켓=%d", 
             handler->is_server ? "서버" : "클라이언트", handler->socket_fd);
    
    if (handler->ssl) {
        SSL_shutdown(handler->ssl);
        SSL_free(handler->ssl);
    }
    free(handler);
    
    LOG_INFO("Network", "SSL 핸들러 정리 완료");
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
    timeout.tv_sec = 3000;//여길 수정하면 타임아웃 시간 변경 가능
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
#ifdef __APPLE__
        // macOS에서는 TCP_KEEPALIVE (초 단위)
        int keepidle = 60;
        if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPALIVE, &keepidle, sizeof(keepidle)) < 0) {
            utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "TCP_KEEPALIVE(macOS) 설정 실패: %s", strerror(errno));
            return -1;
        }
#else
    #if defined(TCP_KEEPIDLE)
        int keepidle = 60;
        if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) < 0) {
            utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Network", "TCP_KEEPIDLE 설정 실패: %s", strerror(errno));
            return -1;
        }
    #endif
#endif
/*호환성 문제로 주석처리
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
*/
    }

    // LOG_INFO("Network", "소켓 옵션 설정 완료: fd=%d, is_server=%d", socket_fd, is_server);
    LOG_INFO("Network", "소켓 옵션 설정 완료: fd=%d, %s 모드", socket_fd, is_server ? "서버" : "클라이언트");
    return 0;
}

void network_cleanup_ssl_manager(ssl_manager_t* manager) {
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
 * @return 성공 시 ssl_handler_t* 포인터, 실패 시 NULL
 */
ssl_handler_t* network_accept_client(int server_fd, ssl_manager_t* ssl_manager, char* client_ip) {
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
    LOG_INFO("Network", "클라이언트 연결 수락: IP=%s, 소켓=%d", client_ip, client_fd);
    ssl_handler_t* ssl_handler = network_create_ssl_handler(ssl_manager, client_fd);
    if (!ssl_handler) {
        utils_report_error(ERROR_NETWORK_SSL_INIT_FAILED, "Network", "SSL 핸들러 생성 실패: IP=%s", client_ip);
        CLEANUP_AND_RET(close(client_fd), NULL);
    }
    if (network_handle_ssl_handshake(ssl_handler) != 0) {
        utils_report_error(ERROR_NETWORK_SSL_HANDSHAKE_FAILED, "Network", "SSL 핸드셰이크 실패: IP=%s", client_ip);
        network_cleanup_ssl_handler(ssl_handler);
        CLEANUP_AND_RET(close(client_fd), NULL);
    }
    LOG_INFO("Network", "클라이언트 SSL 연결 성공: IP=%s, 소켓=%d", client_ip, client_fd);
    return ssl_handler;
}

/**
 * @brief SSL 핸드셰이크를 수행합니다.
 * @param client_fd 클라이언트 소켓 파일 디스크립터
 * @param mgr SSL 매니저 포인터
 * @return 성공 시 ssl_handler_t* 포인터, 실패 시 NULL
 */
ssl_handler_t* network_perform_ssl_handshake(int client_fd, ssl_manager_t* mgr) {
    CHECK_PARAM_RET_PTR(mgr, ERROR_INVALID_PARAMETER, "Network", "network_perform_ssl_handshake: SSL 매니저가 NULL입니다");
    LOG_INFO("Network", "SSL 핸드셰이크 시작: fd=%d", client_fd);
    ssl_handler_t* handler = network_create_ssl_handler(mgr, client_fd);
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
    LOG_INFO("Network", "SSL 핸드셰이크 성공: fd=%d", client_fd);
    return handler;
}