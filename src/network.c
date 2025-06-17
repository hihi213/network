#include "../include/network.h"
#include "../include/logger.h"
#include "../include/message.h"


/* SSL 컨텍스트 초기화 */
static int init_ssl_context(SSLManager* manager, const char* cert_file, const char* key_file) {
    LOG_INFO("Network", "SSL 컨텍스트 초기화 시작");
    if (!manager || !cert_file || !key_file) {
        LOG_ERROR("SSL", "잘못된 파라미터");
        return -1;
    }

    // OpenSSL 3.0 이상에서는 TLS_method() 사용
    LOG_INFO("SSL", "SSL 컨텍스트 생성 시도");
    manager->ctx = SSL_CTX_new(TLS_server_method());
    if (!manager->ctx) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        LOG_ERROR("SSL", "SSL 컨텍스트 생성 실패: %s", err_buf);
        return -1;
    }
    LOG_INFO("SSL", "SSL 컨텍스트 생성 성공");

    // TLS 1.2 이상만 사용하도록 설정
    SSL_CTX_set_min_proto_version(manager->ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(manager->ctx, TLS1_3_VERSION);

    // 인증서 로드
    LOG_INFO("SSL", "인증서 파일 로드 시도: %s", cert_file);
    if (SSL_CTX_use_certificate_file(manager->ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        LOG_ERROR("SSL", "인증서 파일 로드 실패: %s", err_buf);
        SSL_CTX_free(manager->ctx);
        manager->ctx = NULL;
        return -1;
    }
    LOG_INFO("SSL", "인증서 파일 로드 성공");

    // 개인키 로드
    LOG_INFO("SSL", "개인키 파일 로드 시도: %s", key_file);
    if (SSL_CTX_use_PrivateKey_file(manager->ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        LOG_ERROR("SSL", "개인키 파일 로드 실패: %s", err_buf);
        SSL_CTX_free(manager->ctx);
        manager->ctx = NULL;
        return -1;
    }
    LOG_INFO("SSL", "개인키 파일 로드 성공");

    // 인증서와 개인키 매칭 확인
    LOG_INFO("SSL", "인증서와 개인키 매칭 확인");
    if (!SSL_CTX_check_private_key(manager->ctx)) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        LOG_ERROR("SSL", "인증서와 개인키가 매칭되지 않음: %s", err_buf);
        SSL_CTX_free(manager->ctx);
        manager->ctx = NULL;
        return -1;
    }
    LOG_INFO("SSL", "인증서와 개인키 매칭 확인 성공");

    // 보안 설정
    LOG_INFO("SSL", "보안 설정 적용");
    SSL_CTX_set_options(manager->ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
    SSL_CTX_set_mode(manager->ctx, SSL_MODE_AUTO_RETRY);
    
    // 자체 서명된 인증서 허용
    SSL_CTX_set_verify(manager->ctx, SSL_VERIFY_NONE, NULL);
    
    LOG_INFO("SSL", "보안 설정 적용 완료");

    LOG_INFO("SSL", "SSL 컨텍스트 초기화 완료");
    return 0;
}

/* SSL 관리자 초기화 */
int init_ssl_manager(SSLManager* manager, bool is_server, const char* cert_file, const char* key_file) {
    LOG_INFO("SSL", "init_ssl_manager 진입");
    
    // manager 포인터 검증
    if (!manager) {
        LOG_ERROR("SSL", "manager 포인터가 NULL입니다");
        return -1;
    }

    // manager 구조체 초기화
    memset(manager, 0, sizeof(SSLManager));
    manager->is_server = is_server;
    manager->ctx = NULL;  // SSL 컨텍스트 명시적 초기화

    // OpenSSL 초기화
    LOG_INFO("SSL", "OpenSSL 초기화 시작");
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    SSL_library_init();
    LOG_INFO("SSL", "OpenSSL 초기화 완료");

    // OpenSSL 버전 확인
    LOG_INFO("SSL", "OpenSSL 버전 확인");
    const char* version = OpenSSL_version(OPENSSL_VERSION);
    if (!version) {
        LOG_ERROR("SSL", "OpenSSL 버전 확인 실패");
        return -1;
    }
    LOG_INFO("SSL", "OpenSSL 버전: %s", version);

    // OpenSSL 에러 큐 초기화
    ERR_clear_error();
    LOG_INFO("SSL", "OpenSSL 에러 큐 초기화 완료");

    if (is_server) {
        LOG_INFO("SSL", "서버 모드: 인증서/키 파일 확인");
        if (!cert_file || !key_file) {
            LOG_ERROR("SSL", "서버 모드에서는 인증서와 키 파일이 필요합니다");
            return -1;
        }

        // 파일 존재 여부 확인
        if (access(cert_file, F_OK) == -1) {
            LOG_ERROR("SSL", "인증서 파일이 존재하지 않음: %s", cert_file);
            return -1;
        }
        if (access(key_file, F_OK) == -1) {
            LOG_ERROR("SSL", "키 파일이 존재하지 않음: %s", key_file);
            return -1;
        }

        LOG_INFO("SSL", "인증서 파일: %s, 키 파일: %s", cert_file, key_file);
        
        // 문자열 복사 전 버퍼 크기 확인
        if (strlen(cert_file) >= sizeof(manager->cert_file) || 
            strlen(key_file) >= sizeof(manager->key_file)) {
            LOG_ERROR("SSL", "파일 경로가 너무 깁니다");
            return -1;
        }
        
        // 안전한 문자열 복사
        strncpy(manager->cert_file, cert_file, sizeof(manager->cert_file) - 1);
        manager->cert_file[sizeof(manager->cert_file) - 1] = '\0';
        
        strncpy(manager->key_file, key_file, sizeof(manager->key_file) - 1);
        manager->key_file[sizeof(manager->key_file) - 1] = '\0';

        LOG_INFO("SSL", "init_ssl_context 호출 전");
        int result = init_ssl_context(manager, cert_file, key_file);
        if (result != 0) {
            unsigned long err = ERR_get_error();
            char err_buf[256];
            ERR_error_string_n(err, err_buf, sizeof(err_buf));
            LOG_ERROR("SSL", "SSL 컨텍스트 초기화 실패: %s", err_buf);
        }
        return result;
    } else {
        // 클라이언트 모드에서는 인증서 검증만 수행
        manager->ctx = SSL_CTX_new(TLS_client_method());
        if (!manager->ctx) {
            unsigned long err = ERR_get_error();
            char err_buf[256];
            ERR_error_string_n(err, err_buf, sizeof(err_buf));
            LOG_ERROR("SSL", "SSL 컨텍스트 생성 실패: %s", err_buf);
            return -1;
        }

        // TLS 1.2 이상만 사용하도록 설정
        SSL_CTX_set_min_proto_version(manager->ctx, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(manager->ctx, TLS1_3_VERSION);

        // 자체 서명된 인증서를 신뢰하도록 설정
        SSL_CTX_set_verify(manager->ctx, SSL_VERIFY_NONE, NULL);
        
        // 추가 SSL 옵션 설정
        SSL_CTX_set_options(manager->ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
        SSL_CTX_set_mode(manager->ctx, SSL_MODE_AUTO_RETRY);
        
        LOG_INFO("SSL", "클라이언트 SSL 컨텍스트 초기화 완료");
        return 0;
    }
}

/* 서버 소켓 초기화 */
int init_server_socket(int port) {
    LOG_INFO("Network", "서버 소켓 초기화 시작: 포트=%d", port);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_ERROR("Network", "서버 소켓 생성 실패");
        return -1;
    }

    // 소켓 옵션 설정
    if (set_socket_options(server_fd) < 0) {
        LOG_ERROR("Network", "소켓 옵션 설정 실패: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Network", "서버 소켓 바인딩 실패");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, SOMAXCONN) < 0) {
        LOG_ERROR("Network", "서버 소켓 리스닝 실패");
        close(server_fd);
        return -1;
    }

    LOG_INFO("Network", "서버 소켓 초기화 완료");
    return server_fd;
}

/* 클라이언트 소켓 초기화 */
int init_client_socket(const char* server_ip, int port) {
    LOG_INFO("Network", "클라이언트 소켓 초기화 시작: 서버=%s, 포트=%d", server_ip, port);
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        LOG_ERROR("Network", "클라이언트 소켓 생성 실패");
        return -1;
    }

    // 소켓 옵션 설정
    if (set_socket_options(client_fd) < 0) {
        LOG_ERROR("Network", "소켓 옵션 설정 실패: %s", strerror(errno));
        close(client_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        LOG_ERROR("Network", "IP 주소 변환 실패: %s", strerror(errno));
        close(client_fd);
        return -1;
    }

    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Network", "서버 연결 실패");
        close(client_fd);
        return -1;
    }

    LOG_INFO("Network", "클라이언트 소켓 초기화 완료");
    return client_fd;
}

/* SSL 연결 설정 */
SSL* setup_ssl_connection(SSL_CTX* ctx, int socket_fd) {
    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        LOG_ERROR("SSL", "SSL 객체 생성 실패: %s", ERR_error_string(ERR_get_error(), NULL));
        return NULL;
    }

    if (!SSL_set_fd(ssl, socket_fd)) {
        LOG_ERROR("SSL", "SSL 소켓 설정 실패: %s", ERR_error_string(ERR_get_error(), NULL));
        SSL_free(ssl);
        return NULL;
    }

    return ssl;
}

/* SSL 핸드셰이크 처리 함수 */
int handle_ssl_handshake(SSLHandler* handler) {
    if (!handler || !handler->ssl) {
        LOG_ERROR("SSL", "잘못된 SSL 핸들러");
        return -1;
    }

    LOG_INFO("Network", "SSL 핸드셰이크 시작");

    int ret;
    if (handler->is_server) {
        ret = SSL_accept(handler->ssl);
    } else {
        ret = SSL_connect(handler->ssl);
    }

    if (ret <= 0) {
        int ssl_error = SSL_get_error(handler->ssl, ret);
        unsigned long err_code;
        char err_buf[256];

        switch (ssl_error) {
            case SSL_ERROR_SSL:
                err_code = ERR_get_error();
                ERR_error_string_n(err_code, err_buf, sizeof(err_buf));
                LOG_ERROR("SSL", "SSL 핸드셰이크 실패 (SSL_ERROR_SSL): %s", err_buf);
                break;
            case SSL_ERROR_SYSCALL:
                err_code = ERR_get_error(); // EOF의 경우 0일 수 있음
                if (err_code == 0) {
                    if (ret == 0) {
                        LOG_ERROR("SSL", "SSL 핸드셰이크 실패: 연결이 예기치 않게 종료되었습니다 (EOF).");
                    } else {
                        // 시스템 콜 오류는 errno를 사용
                        LOG_ERROR("SSL", "SSL 핸드셰이크 실패 (SSL_ERROR_SYSCALL): %s", strerror(errno));
                    }
                } else {
                    ERR_error_string_n(err_code, err_buf, sizeof(err_buf));
                    LOG_ERROR("SSL", "SSL 핸드셰이크 실패 (SSL_ERROR_SYSCALL): %s", err_buf);
                }
                break;
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                // 이 경우는 논블로킹 I/O를 위한 것으로, 오류가 아닐 수 있음
                // 현재의 블로킹 모델에서는 예기치 않은 상태로 간주
                LOG_WARNING("SSL", "SSL 핸드셰이크 중 블로킹 발생 (WANT_READ/WANT_WRITE)");
                return -1;
            default:
                LOG_ERROR("SSL", "SSL 핸드셰이크 실패: 알 수 없는 SSL 오류 코드 %d", ssl_error);
                break;
        }
        return -1;
    }

    // SSL 상태 확인
    int ssl_state = SSL_get_state(handler->ssl);
    LOG_INFO("Network", "SSL 핸드셰이크 후 상태: %d", ssl_state);

    // TLS_ST_OK 상태가 될 때까지 대기
    int max_retries = 5;
    int retry_count = 0;
    while (ssl_state != TLS_ST_OK && retry_count < max_retries) {
        usleep(100000); // 100ms 대기
        ssl_state = SSL_get_state(handler->ssl);
        LOG_INFO("Network", "SSL 상태 확인 중: %d (시도 %d/%d)", ssl_state, retry_count + 1, max_retries);
        retry_count++;
    }

    if (ssl_state != TLS_ST_OK) {
        LOG_ERROR("Network", "SSL 핸드셰이크가 완료되지 않음 (최종 상태: %d)", ssl_state);
        return -1;
    }

    // SSL 세션 정보 로깅
    const SSL_CIPHER* cipher = SSL_get_current_cipher(handler->ssl);
    if (cipher) {
        LOG_INFO("Network", "사용 중인 암호화 방식: %s", SSL_CIPHER_get_name(cipher));
    }

    update_ssl_activity(handler);
    LOG_INFO("Network", "SSL 핸드셰이크 완료");
    return 0;
}

/* SSL 연결 종료 */
void close_connection(SSL* ssl, int socket_fd) {
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (socket_fd >= 0) {
        close(socket_fd);
    }
}

/* SSL 컨텍스트 정리 */
void cleanup_ssl_context(SSL_CTX* ctx) {
    if (ctx) {
        SSL_CTX_free(ctx);
    }
}

/* 논블로킹 소켓 설정 함수 */
int set_nonblocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        LOG_ERROR("Network", "소켓 플래그 가져오기 실패: %s", strerror(errno));
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(socket_fd, F_SETFL, flags) == -1) {
        LOG_ERROR("Network", "소켓 논블로킹 설정 실패: %s", strerror(errno));
        return -1;
    }

    return 0;
}

SSLHandler* create_ssl_handler(SSLManager* manager, int socket_fd) {
    if (!manager || !manager->ctx) {
        LOG_ERROR("SSL", "잘못된 SSL Manager 또는 Context");
        return NULL;
    }

    SSLHandler* handler = (SSLHandler*)malloc(sizeof(SSLHandler));
    if (!handler) {
        LOG_ERROR("SSL", "SSL 핸들러 메모리 할당 실패");
        return NULL;
    }
    memset(handler, 0, sizeof(SSLHandler));

    handler->socket_fd = socket_fd;
    handler->ctx = manager->ctx;
    handler->handshake_state = SSL_HANDSHAKE_INIT;
    handler->is_server = manager->is_server;  // 서버/클라이언트 모드 설정

    handler->ssl = SSL_new(manager->ctx);
    if (!handler->ssl) {
        LOG_ERROR("SSL", "SSL 객체 생성 실패: %s", ERR_error_string(ERR_get_error(), NULL));
        free(handler);
        return NULL;
    }

    if (!SSL_set_fd(handler->ssl, socket_fd)) {
        LOG_ERROR("SSL", "SSL 소켓 설정 실패: %s", ERR_error_string(ERR_get_error(), NULL));
        SSL_free(handler->ssl);
        free(handler);
        return NULL;
    }
    update_ssl_activity(handler);
    return handler;
}

void cleanup_ssl_handler(SSLHandler* handler) {
    if (!handler) return;
    if (handler->ssl) {
        SSL_shutdown(handler->ssl); // SSL 연결 종료
        SSL_free(handler->ssl);     // SSL 객체 해제
        handler->ssl = NULL;
    }
    free(handler); // SSLHandler 자체의 메모리 해제
}

/* 유틸리티 함수 */
void update_ssl_activity(SSLHandler* handler) {
    if (handler) {
        handler->last_activity = time(NULL);
    }
}

int set_socket_options(int socket_fd) {
    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Network", "소켓 옵션 (SO_REUSEADDR) 설정 실패: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int set_keepalive(int socket_fd, int keepidle, int keepinterval, int keepcount) {
    int optval = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
        LOG_ERROR("Network", "소켓 옵션 (SO_KEEPALIVE) 설정 실패: %s", strerror(errno));
        return -1;
    }

    // macOS에서는 TCP_KEEPALIVE 옵션만 지원
    optval = keepidle;
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPALIVE, &optval, sizeof(optval)) < 0) {
        LOG_ERROR("Network", "소켓 옵션 (TCP_KEEPALIVE) 설정 실패: %s", strerror(errno));
        return -1;
    }

    optval = keepinterval;
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPALIVE, &optval, sizeof(optval)) < 0) {
        LOG_ERROR("Network", "소켓 옵션 (TCP_KEEPALIVE) 설정 실패: %s", strerror(errno));
        return -1;
    }

    optval = keepcount;
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPALIVE, &optval, sizeof(optval)) < 0) {
        LOG_ERROR("Network", "소켓 옵션 (TCP_KEEPALIVE) 설정 실패: %s", strerror(errno));
        return -1;
    }
    return 0;
}

const char* get_ssl_error(void) {
    unsigned long err_code = ERR_get_error();
    if (err_code == 0) {
        return "No SSL error";
    }
    return ERR_error_string(err_code, NULL);
}

time_t get_ssl_last_activity(const SSLHandler* handler) {
    if (handler) {
        return handler->last_activity;
    }
    return 0;
}

void cleanup_ssl_manager(SSLManager* manager) {
    if (!manager) return;

    // SSL 컨텍스트 정리
    if (manager->ctx) {
        SSL_CTX_free(manager->ctx);
        manager->ctx = NULL;
    }

    // OpenSSL 정리
    EVP_cleanup();
    ERR_free_strings();
}

int send_message(SSL* ssl, const Message* message) {
    if (!ssl || !message) {
        LOG_ERROR("Network", "잘못된 파라미터");
        return -1;
    }

    // 1. 메시지 타입 전송
    uint32_t type = htonl(message->type);
    int ret = SSL_write(ssl, &type, sizeof(type));
    if (ret <= 0) {
        LOG_ERROR("Network", "메시지 타입 전송 실패");
        return -1;
    }

    // 2. 인자 개수 전송
    uint32_t arg_count = htonl(message->arg_count);
    ret = SSL_write(ssl, &arg_count, sizeof(arg_count));
    if (ret <= 0) {
        LOG_ERROR("Network", "인자 개수 전송 실패");
        return -1;
    }

    // 3. 각 인자 전송
    for (int i = 0; i < message->arg_count; i++) {
        uint32_t arg_len = htonl(strlen(message->args[i]));
        ret = SSL_write(ssl, &arg_len, sizeof(arg_len));
        if (ret <= 0) {
            LOG_ERROR("Network", "인자 길이 전송 실패");
            return -1;
        }

        ret = SSL_write(ssl, message->args[i], strlen(message->args[i]));
        if (ret <= 0) {
            LOG_ERROR("Network", "인자 내용 전송 실패");
            return -1;
        }
    }

    // 4. 데이터 길이와 내용 전송
    uint32_t data_len = htonl(strlen(message->data));
    ret = SSL_write(ssl, &data_len, sizeof(data_len));
    if (ret <= 0) {
        LOG_ERROR("Network", "데이터 길이 전송 실패");
        return -1;
    }

    if (strlen(message->data) > 0) {
        ret = SSL_write(ssl, message->data, strlen(message->data));
        if (ret <= 0) {
            LOG_ERROR("Network", "데이터 내용 전송 실패");
            return -1;
        }
    }

    LOG_INFO("Network", "메시지 전송 완료: 타입=%s", get_message_type_string(message->type));
    return 0;
}

/* 연결 상태 확인 함수 */
bool is_connection_alive(SSL* ssl, int socket_fd) {
    if (!ssl || socket_fd < 0) return false;
    
    char buf[1];
    int ret = SSL_peek(ssl, buf, 1);
        if (ret <= 0) {
        int ssl_error = SSL_get_error(ssl, ret);
        if (ssl_error == SSL_ERROR_NONE || ssl_error == SSL_ERROR_WANT_READ) {
            return true;
        }
        return false;
    }
    return true;
}

