
#include "../include/logger.h"
#include "../include/message.h"
#include "../include/network.h"
#include "../include/session.h"
#include "../include/ui.h"
#include "../include/performance.h"
#include "../include/resource.h"
#include "../include/reservation.h"


/* 전역 변수 */
static SSL_CTX* ssl_ctx;
static int server_sock;
static ResourceManager* resource_manager = NULL;
static ReservationManager* reservation_manager;
static SessionManager* session_manager = NULL;
static BufferPool buffer_pool;
static BatchBuffer* batch_buffer = NULL;
static PerformanceStats* perf_stats = NULL;
static bool running = true;
static SSLManager ssl_manager;
static Device devices[MAX_DEVICES];
static pthread_mutex_t shutdown_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool shutdown_requested = false;
static pthread_cond_t shutdown_cond = PTHREAD_COND_INITIALIZER;

/* 종료 상태 구조체 */
typedef struct {
    bool requested;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} ShutdownState;

static ShutdownState shutdown_state;
/* 클라이언트 정보 구조체 */
typedef struct {
    int socket_fd;
    SSL* ssl;
    SSLHandler* ssl_handler; 
    char ip[INET_ADDRSTRLEN];
    int port;
    SessionState state;
    char username[MAX_USERNAME_LENGTH];
    time_t last_activity;
} Client;

/* 시그널 핸들러 */
static void signal_handler(int signum) {
    LOG_INFO("Server", "시그널 수신: %d", signum);
    pthread_mutex_lock(&shutdown_state_mutex);
    shutdown_requested = true;
    pthread_cond_signal(&shutdown_cond);
    pthread_mutex_unlock(&shutdown_state_mutex);
}

/* 에러 응답 전송 */
static int send_error_response(SSL* ssl, const char* error_message) {
    if (!ssl || !error_message) {
        LOG_ERROR("Server", "잘못된 파라미터");
        return -1;
    }

    Message* response = create_message(MSG_ERROR, NULL);
    if (!response) {
        LOG_ERROR("Server", "응답 메시지 생성 실패");
        return -1;
    }

    strncpy(response->args[0], error_message, MAX_ARG_LENGTH - 1);
    response->arg_count = 1;

    int ret = send_message(ssl, response);
    cleanup_message(response);
    free(response);

    return ret;
}

/* 예약 요청 처리 */
static int handle_reserve_request(Client* client, const Message* message) {
    if (!client || !message || message->arg_count < 3) {
        LOG_ERROR("Server", "잘못된 예약 요청");
        return -1;
    }

    const char* device_id = message->args[0];
    int duration = atoi(message->args[1]);
    const char* purpose = message->args[2];

    // 장비 존재 여부 확인
    Device* device = get_device(resource_manager, device_id);
    if (!device) {
        LOG_ERROR("Server", "존재하지 않는 장비: %s", device_id);
        return send_error_response(client->ssl, "존재하지 않는 장비입니다.");
    }

    // 장비 사용 가능 여부 확인
    if (!is_device_available(resource_manager, device_id)) {
        LOG_ERROR("Server", "장비 사용 불가: %s", device_id);
        return send_error_response(client->ssl, "현재 사용 중인 장비입니다.");
    }

    // 예약 생성
    time_t start_time = time(NULL);
    time_t end_time = start_time + (duration * 60); // 분을 초로 변환

    if (!create_reservation(reservation_manager, device_id, client->username,
                          start_time, end_time, purpose)) {
        LOG_ERROR("Server", "예약 생성 실패: %s", device_id);
        return send_error_response(client->ssl, "예약 생성에 실패했습니다.");
    }

    // 장비 상태 업데이트
    if (!update_device_status(resource_manager, device_id, "reserved", client->username)) {
        LOG_ERROR("Server", "장비 상태 업데이트 실패: %s", device_id);
        // 예약은 이미 생성되었으므로 롤백하지 않음
    }

    // 성공 응답 전송
    Message* response = create_message(MSG_RESERVE_RESPONSE, NULL);
    if (!response) {
        LOG_ERROR("Server", "응답 메시지 생성 실패");
        return -1;
    }

    strncpy(response->args[0], "success", MAX_ARG_LENGTH - 1);
    response->arg_count = 1;

    int ret = send_message(client->ssl, response);
    cleanup_message(response);
    free(response);

    LOG_INFO("Server", "예약 성공: 장비=%s, 사용자=%s, 시간=%d분", 
             device_id, client->username, duration);

    return ret;
}

/* 예약 취소 처리 */
static int handle_cancel_request(Client* client, const Message* message) {
    if (!client || !message || message->arg_count < 1) {
        LOG_ERROR("Server", "잘못된 취소 요청");
        return -1;
    }

    const char* device_id = message->args[0];

    // 예약 존재 여부 확인
    Reservation reservations[MAX_RESERVATIONS];
    int count = get_device_reservations(reservation_manager, device_id, reservations, MAX_RESERVATIONS);
    if (count <= 0) {
        LOG_ERROR("Server", "존재하지 않는 예약: %s", device_id);
        return send_error_response(client->ssl, "존재하지 않는 예약입니다.");
    }
    Reservation* reservation = &reservations[0];

    // 예약자 확인
    if (strcmp(reservation->username, client->username) != 0) {
        LOG_ERROR("Server", "예약자 불일치: %s != %s", 
                 reservation->username, client->username);
        return send_error_response(client->ssl, "자신의 예약만 취소할 수 있습니다.");
    }

    // 예약 취소
    if (!cancel_reservation(reservation_manager, reservation->id, client->username)) {
        LOG_ERROR("Server", "예약 취소 실패: %s", device_id);
        return send_error_response(client->ssl, "예약 취소에 실패했습니다.");
    }

    // 장비 상태 업데이트
    if (!update_device_status(resource_manager, device_id, "available", NULL)) {
        LOG_ERROR("Server", "장비 상태 업데이트 실패: %s", device_id);
        // 예약은 이미 취소되었으므로 롤백하지 않음
    }

    // 성공 응답 전송
    Message* response = create_message(MSG_CANCEL_RESPONSE, NULL);
    if (!response) {
        LOG_ERROR("Server", "응답 메시지 생성 실패");
        return -1;
    }

    strncpy(response->args[0], "success", MAX_ARG_LENGTH - 1);
    response->arg_count = 1;

    int ret = send_message(client->ssl, response);
    cleanup_message(response);
    free(response);

    LOG_INFO("Server", "예약 취소 성공: 장비=%s, 사용자=%s", 
             device_id, client->username);

    return ret;
}

/* 로그아웃 처리 */
static int handle_logout(Client* client) {
    if (!client) {
        LOG_ERROR("Server", "잘못된 클라이언트");
        return -1;
    }

    if (client->state == SESSION_LOGGED_IN) {
        // 세션 종료
        close_session(session_manager, client->username);
        client->state = SESSION_DISCONNECTED;
        
        LOG_INFO("Server", "로그아웃 성공: %s", client->username);
    }

    return 0;
}

/* 클라이언트 메시지 처리 함수 */
static int handle_client_message(SSL* ssl, const Message* message) {
    if (!ssl || !message) {
        LOG_ERROR("Server", "잘못된 파라미터");
        return -1;
    }

    LOG_INFO("Server", "메시지 수신: 타입=%s", get_message_type_string(message->type));

    switch (message->type) {
        case MSG_STATUS_REQUEST: {
            LOG_INFO("Server", "장비 상태 요청 처리");
            int device_count = get_device_list(resource_manager, devices, MAX_DEVICES);
            LOG_INFO("Server", "장비 개수: %d", device_count);
            
            if (device_count > 0) {
                Message* response = create_status_response_message(devices, device_count);
                if (response) {
                    if (send_message(ssl, response) < 0) {
                        LOG_ERROR("Server", "장비 상태 응답 전송 실패");
                    }
                    cleanup_message(response);
                    free(response);
                } else {
                    LOG_ERROR("Server", "장비 상태 응답 메시지 생성 실패");
                    send_error_response(ssl, "장비 상태 응답 생성 실패");
                }
            } else {
                LOG_ERROR("Server", "등록된 장비가 없습니다.");
                send_error_response(ssl, "등록된 장비가 없습니다.");
            }
            break;
        }
        case MSG_RESERVE_REQUEST: {
            if (message->arg_count < 1) {
                LOG_ERROR("Server", "잘못된 예약 요청: 인자 부족");
                send_error_response(ssl, "잘못된 예약 요청");
                break;
            }
            
            const char* device_id = message->args[0];
            LOG_INFO("Server", "장비 예약 요청: %s", device_id);
            
            // 장비 상태 확인 및 예약 처리
            int device_index = -1;
            for (int i = 0; i < MAX_DEVICES; i++) {
                if (strcmp(devices[i].id, device_id) == 0) {
                    device_index = i;
                    break;
                }
            }
            
            if (device_index == -1) {
                LOG_ERROR("Server", "존재하지 않는 장비: %s", device_id);
                send_error_response(ssl, "존재하지 않는 장비입니다.");
                break;
            }
            
            if (devices[device_index].status != DEVICE_AVAILABLE) {
                LOG_ERROR("Server", "예약 불가능한 장비: %s", device_id);
                send_error_response(ssl, "예약할 수 없는 장비입니다.");
                break;
            }
            
            // 예약 처리
            devices[device_index].status = DEVICE_RESERVED;
            strncpy(devices[device_index].reserved_by, message->args[1], MAX_USERNAME_LENGTH - 1);
            devices[device_index].reserved_by[MAX_USERNAME_LENGTH - 1] = '\0';
            
            // 예약 성공 응답
            Message* response = create_message(MSG_RESERVE_RESPONSE, "success");
            if (response) {
                if (send_message(ssl, response) < 0) {
                    LOG_ERROR("Server", "예약 응답 전송 실패");
                }
                cleanup_message(response);
                free(response);
            }
            break;
        }
        case MSG_PING:
            LOG_DEBUG("Server", "PING 메시지 수신");
            Message* pong = create_message(MSG_PONG, NULL);
            if (pong) {
                if (send_message(ssl, pong) < 0) {
                    LOG_ERROR("Server", "PONG 메시지 전송 실패");
                }
                cleanup_message(pong);
                free(pong);
            }
            break;
        default:
            LOG_ERROR("Server", "알 수 없는 메시지 타입: %d", message->type);
            send_error_response(ssl, "알 수 없는 메시지 타입");
            break;
    }

    return 0;
}

/* 클라이언트 스레드 함수 (세션 갱신 로직 추가된 최종 버전) */
static void* client_thread_func(void* arg) {
    Client* client = (Client*)arg;
    const int TIMEOUT_MS = 600000; // 10분 (600초)
    time_t last_activity_time = time(NULL);

    struct pollfd fds[1];
    fds[0].fd = client->socket_fd;
    fds[0].events = POLLIN;

    LOG_INFO("Server", "클라이언트 스레드 시작: %s", client->ip);

    Message* welcome_ping_msg = create_message(MSG_PING, NULL);
    if (welcome_ping_msg) {
        if (send_message(client->ssl, welcome_ping_msg) < 0) {
            LOG_ERROR("Server", "초기 PING 메시지 전송 실패. 스레드를 종료합니다.");
            cleanup_message(welcome_ping_msg);
            free(welcome_ping_msg);
            
            // 자원 정리
            if (client->state == SESSION_LOGGED_IN) {
                close_session(session_manager, client->username);
            }
            if (client->ssl_handler) {
                cleanup_ssl_handler(client->ssl_handler);
                client->ssl_handler = NULL;  // 핸들러 정리 후 NULL로 설정
            }
            close(client->socket_fd);
            free(client);
            return NULL;
        }
        
        cleanup_message(welcome_ping_msg);
        free(welcome_ping_msg);
        last_activity_time = time(NULL);
    }

    while (running) {
        int ret = poll(fds, 1, 1000);
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("Server", "poll 실패: %s", strerror(errno));
            break;
        }

        time_t current_time = time(NULL);

        if (ret > 0 && (fds[0].revents & POLLIN)) {
            Message* received_msg = (Message*)malloc(sizeof(Message));
            if (!received_msg) {
                LOG_ERROR("Server", "메시지 메모리 할당 실패");
                break;
            }
            if (receive_message(client->ssl, received_msg) < 0) {
                LOG_ERROR("Server", "메시지 수신 실패 또는 연결 종료됨");
                free(received_msg);
                break;
            }

            last_activity_time = current_time;

            if (received_msg->type == MSG_PING_RESPONSE) {
                LOG_DEBUG("Server", "PONG 수신: %s", client->ip);
                cleanup_message(received_msg);
                free(received_msg);
                continue;
            }

            if (handle_client_message(client->ssl, received_msg) < 0) {
                LOG_ERROR("Server", "메시지 처리 실패");
                cleanup_message(received_msg);
                free(received_msg);
                break;
            }
            
            cleanup_message(received_msg);
            free(received_msg);
        }
        
        if (current_time - last_activity_time >= TIMEOUT_MS / 1000) {
            LOG_WARNING("Server", "클라이언트 타임아웃: %s", client->ip);
            break;
        }
    }

    // 연결 종료 시 정리
    LOG_INFO("Server", "클라이언트 연결 종료 및 자원 정리: %s", client->ip);
    if (client->state == SESSION_LOGGED_IN) {
        close_session(session_manager, client->username);
    }
    if (client->ssl_handler) {
        cleanup_ssl_handler(client->ssl_handler);
        client->ssl_handler = NULL;  // 핸들러 정리 후 NULL로 설정
    }
    close(client->socket_fd);
    free(client);

    return NULL;
}

/* 서버 초기화 함수 */
static int init_server(int port) {
    LOG_INFO("Server", "서버 초기화 시작: 포트=%d", port);
    
    struct sockaddr_in server_addr;
    int ret;

    LOG_INFO("Server", "SSL 관리자 초기화 직전");
    // SSL 컨텍스트 초기화
    ret = init_ssl_manager(&ssl_manager, true, "certs/server.crt", "certs/server.key");
    LOG_INFO("Server", "init_ssl_manager 호출 반환: %d", ret);
    if (ret < 0) {
        LOG_ERROR("SSL", "SSL 관리자 초기화 실패");
        return -1;
    }
    LOG_INFO("Server", "SSL 관리자 초기화 성공");
    ssl_ctx = ssl_manager.ctx;

    LOG_INFO("Server", "서버 소켓 생성 시작");
    // 서버 소켓 생성
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        LOG_ERROR("Server", "서버 소켓 생성 실패");
        return -1;
    }
    LOG_INFO("Server", "서버 소켓 생성 성공");

    LOG_INFO("Server", "소켓 옵션 설정 시작");
    // 소켓 옵션 설정
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Server", "소켓 옵션 설정 실패");
        close(server_sock);
        return -1;
    }
    LOG_INFO("Server", "소켓 옵션 설정 성공");

    LOG_INFO("Server", "서버 주소 설정 시작");
    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    LOG_INFO("Server", "서버 주소 설정 성공");

    LOG_INFO("Server", "소켓 바인딩 시작");
    // 바인딩
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Server", "서버 소켓 바인딩 실패");
        close(server_sock);
        return -1;
    }
    LOG_INFO("Server", "소켓 바인딩 성공");

    LOG_INFO("Server", "리스닝 시작");
    // 리스닝
    if (listen(server_sock, SOMAXCONN) < 0) {
        LOG_ERROR("Server", "서버 소켓 리스닝 실패");
        close(server_sock);
        return -1;
    }
    LOG_INFO("Server", "리스닝 성공");

    LOG_INFO("Server", "논블로킹 설정 건너뜀 (블로킹 모드로 동작)");
    /*
    // 논블로킹 설정
    if (set_nonblocking(server_sock) < 0) {
        LOG_ERROR("Server", "논블로킹 설정 실패");
        close(server_sock);
        return -1;
    }
    LOG_INFO("Server", "논블로킹 설정 성공");
    */
    return 0;
}


/* 종료 상태 초기화 함수 */
static int init_shutdown_state(void) {
    pthread_mutex_init(&shutdown_state.mutex, NULL);
    pthread_cond_init(&shutdown_state.cond, NULL);
    shutdown_state.requested = false;
    return 0;
}

/* 종료 상태 정리 함수 */
static void cleanup_shutdown_state(void) {
    pthread_mutex_destroy(&shutdown_state.mutex);
    pthread_cond_destroy(&shutdown_state.cond);
}

/* 서버 정리 함수 */
static void cleanup_server(void) {
    LOG_INFO("Server", "서버 정리 시작");

    // 종료 상태 정리
    cleanup_shutdown_state();

    // 성능 통계 정리
    if (perf_stats) {
        cleanup_performance_stats(perf_stats);
        perf_stats = NULL;
    }

    // 배치 버퍼 정리
    cleanup_batch_buffer(batch_buffer);

    // 버퍼 풀 정리
    cleanup_buffer_pool(&buffer_pool);

    // 세션 매니저 정리
    if (session_manager) {
        cleanup_session_manager(session_manager);
        session_manager = NULL;
    }

    // 예약 매니저 정리
    if (reservation_manager) {
        cleanup_reservation_manager(reservation_manager);
        reservation_manager = NULL;
    }

    // 리소스 매니저 정리
    if (resource_manager) {
        cleanup_resource_manager(resource_manager);
        resource_manager = NULL;
    }

    // 서버 소켓 정리
    if (server_sock >= 0) {
        close(server_sock);
        server_sock = -1;
    }

    // SSL 관리자 정리
    cleanup_ssl_manager(&ssl_manager);

    // UI 정리
    cleanup_ui();

    // 로거 정리
    cleanup_logger();

    LOG_INFO("Server", "서버 정리 완료");
}

/* 메인 함수 */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "사용법: %s <포트>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    // 시그널 핸들러 설정
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 로그 디렉토리 생성
    if (mkdir("logs", 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "로그 디렉토리 생성 실패: %s\n", strerror(errno));
        return 1;
    }

    // 로거 초기화
    char log_path[256];
    snprintf(log_path, sizeof(log_path), "logs/server.log");
    if (init_logger(log_path) < 0) {
        fprintf(stderr, "로거 초기화 실패: %s\n", strerror(errno));
        return 1;
    }

    LOG_INFO("Server", "서버 시작: 포트=%d", port);

    // UI 초기화
    LOG_INFO("Server", "UI 초기화 시작");
    if (init_server_ui() < 0) {
        LOG_ERROR("Server", "UI 초기화 실패");
        cleanup_logger();
        return 1;
    }
    if (!global_ui_manager) {
        LOG_ERROR("Server", "global_ui_manager가 NULL입니다!");
        cleanup_logger();
        return 1;
    }
    LOG_INFO("Server", "UI 초기화 성공");

    // 서버 초기화
    LOG_INFO("Server", "서버 초기화 시작");
    if (init_server(port) < 0) {
        LOG_ERROR("Server", "서버 초기화 실패");
        cleanup_ui();
        cleanup_logger();
        return 1;
    }
    LOG_INFO("Server", "init_server() 통과");
    LOG_INFO("Server", "서버 초기화 성공");

    // 자원 관리자 초기화
    LOG_INFO("Server", "자원 관리자 초기화 시작");
    resource_manager = init_resource_manager();
    if (!resource_manager) {
        LOG_ERROR("Server", "자원 관리자 초기화 실패");
        close(server_sock);
        cleanup_ui();
        cleanup_logger();
        return 1;
    }
    LOG_INFO("Server", "init_resource_manager() 통과");

    // 예약 관리자 초기화
    reservation_manager = init_reservation_manager();
    if (!reservation_manager) {
        LOG_ERROR("Server", "예약 관리자 초기화 실패");
        cleanup_resource_manager(resource_manager);
        close(server_sock);
        cleanup_ui();
        cleanup_logger();
        return 1;
    }
    LOG_INFO("Server", "init_reservation_manager() 통과");

    // 세션 관리자 초기화
    session_manager = init_session_manager();
    if (!session_manager) {
        LOG_ERROR("Server", "세션 관리자 초기화 실패");
        cleanup_reservation_manager(reservation_manager);
        cleanup_resource_manager(resource_manager);
        close(server_sock);
        cleanup_ui();
        cleanup_logger();
        return 1;
    }
    LOG_INFO("Server", "init_session_manager() 통과");

    // 버퍼 풀 초기화
    if (init_buffer_pool(&buffer_pool, BUFFER_POOL_SIZE, MAX_BUFFER_SIZE) < 0) {
        LOG_ERROR("Server", "버퍼 풀 초기화 실패");
        cleanup_session_manager(session_manager);
        cleanup_reservation_manager(reservation_manager);
        cleanup_resource_manager(resource_manager);
        close(server_sock);
        cleanup_ui();
        cleanup_logger();
        return 1;
    }
    LOG_INFO("Server", "init_buffer_pool() 통과");

    // 배치 버퍼 초기화
    batch_buffer = init_batch_buffer(MAX_BATCH_SIZE);
    if (!batch_buffer) {
        LOG_ERROR("Server", "배치 버퍼 초기화 실패");
        cleanup_buffer_pool(&buffer_pool);
        cleanup_session_manager(session_manager);
        cleanup_reservation_manager(reservation_manager);
        cleanup_resource_manager(resource_manager);
        close(server_sock);
        cleanup_ui();
        cleanup_logger();
        return 1;
    }
    LOG_INFO("Server", "init_batch_buffer() 통과");

    // 성능 통계 초기화
    perf_stats = init_performance_stats();
    if (!perf_stats) {
        LOG_ERROR("Server", "성능 통계 초기화 실패");
        cleanup_batch_buffer(batch_buffer);
        cleanup_buffer_pool(&buffer_pool);
        cleanup_session_manager(session_manager);
        cleanup_reservation_manager(reservation_manager);
        cleanup_resource_manager(resource_manager);
        close(server_sock);
        cleanup_ui();
        cleanup_logger();
        return 1;
    }
    LOG_INFO("Server", "init_performance_stats() 통과");

    // 종료 상태 초기화
    if (init_shutdown_state() < 0) {
        LOG_ERROR("Server", "종료 상태 초기화 실패");
        cleanup_performance_stats(perf_stats);
        cleanup_batch_buffer(batch_buffer);
        cleanup_buffer_pool(&buffer_pool);
        cleanup_session_manager(session_manager);
        cleanup_reservation_manager(reservation_manager);
        cleanup_resource_manager(resource_manager);
        close(server_sock);
        cleanup_ui();
        cleanup_logger();
        return 1;
    }
    LOG_INFO("Server", "init_shutdown_state() 통과");

    // 메인 이벤트 루프
    struct pollfd fds[1];
    fds[0].fd = server_sock;
    fds[0].events = POLLIN;

    while (running) {
        pthread_mutex_lock(&shutdown_state_mutex);
        if (shutdown_requested) {
            running = false;
        }
        pthread_mutex_unlock(&shutdown_state_mutex);

        if (!running) break; // 종료 요청이 있으면 즉시 루프 종료

        int ret = poll(fds, 1, 1000); // 1초 타임아웃
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("Server", "poll 실패: %s", strerror(errno));
            break;
        }

        if (ret > 0 && (fds[0].revents & POLLIN)) {
            // 클라이언트 연결 수락
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock < 0) {
                if (errno == EINTR) continue;
                LOG_ERROR("Server", "클라이언트 연결 수락 실패: %s", strerror(errno));
                continue;
            }

            // 클라이언트 구조체 생성
            Client* client = (Client*)malloc(sizeof(Client));
            if (!client) {
                LOG_ERROR("Server", "클라이언트 구조체 메모리 할당 실패");
                close(client_sock);
                continue;
            }
            memset(client, 0, sizeof(Client));
            client->socket_fd = client_sock;
            client->port = ntohs(client_addr.sin_port);
            inet_ntop(AF_INET, &client_addr.sin_addr, client->ip, sizeof(client->ip));

            // SSL 핸들러 생성
            SSLHandler* ssl_handler = create_ssl_handler(&ssl_manager, client_sock);
            if (!ssl_handler) {
                LOG_ERROR("Server", "SSL 핸들러 생성 실패");
                free(client);
                close(client_sock);
                continue;
            }

            // SSL 핸드셰이크 수행
            int handshake_ret = handle_ssl_handshake(ssl_handler);
            if (handshake_ret != 0) {
                LOG_ERROR("Server", "SSL 핸드셰이크 실패");
                cleanup_ssl_handler(ssl_handler);
                free(client);
                close(client_sock);
                continue;
            }

            // 핸드셰이크 성공 시 SSL 객체 설정
            client->ssl = ssl_handler->ssl;
            client->ssl_handler = ssl_handler; // 핸들러 자체를 전달

// 클라이언트 스레드 생성
pthread_t thread;
if (pthread_create(&thread, NULL, client_thread_func, client) != 0) {
    LOG_ERROR("Server", "클라이언트 스레드 생성 실패: %s", strerror(errno));
    cleanup_ssl_handler(client->ssl_handler); // 실패 시 핸들러 정리
    free(client);                             // 실패 시 클라이언트 구조체 정리
    close(client_sock);                       // 실패 시 소켓 정리
    continue;
}
pthread_detach(thread);
            
            // 이제 메인 스레드에서는 ssl_handler를 free하지 않습니다.

        }

        // 만료된 예약 처리
        cleanup_expired_reservations(reservation_manager);
        cleanup_expired_sessions(session_manager); // 만료된 세션 정리 호출 추가
        // UI 업데이트
        update_server_status(session_manager->session_count, port);
        Device devices[MAX_DEVICES];
        int device_count = get_device_list(resource_manager, devices, MAX_DEVICES);
        if (device_count >= 0) {
            update_server_devices(devices, device_count);
        }
        refresh_all_windows();
    }

    // 자원 정리
    cleanup_reservation_manager(reservation_manager);
    cleanup_server();

    return 0;
} 