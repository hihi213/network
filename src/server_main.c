/**
 * @file server_main.c
 * @brief 서버 애플리케이션 메인 진입점 - 멀티스레드 클라이언트 처리 및 실시간 모니터링
 * 
 * @details
 * 이 모듈은 장비 예약 시스템의 서버 애플리케이션 메인 진입점입니다:
 * 
 * **핵심 역할:**
 * - 클라이언트 연결 수락 및 SSL 핸드셰이크 처리
 * - 각 클라이언트 연결에 대한 독립적인 스레드 생성
 * - 전역 관리 모듈(리소스, 예약, 세션)의 총괄 관리
 * - 실시간 성능 모니터링 및 UI 갱신
 * 
 * **시스템 아키텍처에서의 위치:**
 * - 애플리케이션 계층: 최상위 진입점
 * - 네트워크 계층: SSL/TLS 보안 통신 관리
 * - 비즈니스 로직 계층: 예약/리소스/세션 관리자 조율
 * - UI 계층: 실시간 서버 상태 모니터링
 * 
 * **주요 특징:**
 * - poll() 기반 이벤트 루프로 클라이언트 연결 처리
 * - 멀티스레드 아키텍처로 동시 클라이언트 지원
 * - 실시간 성능 통계 수집 및 브로드캐스팅
 * - 안전한 종료 및 리소스 정리
 * 
 * @note 서버는 모든 클라이언트 요청의 중앙 처리점 역할을 하며,
 *       시스템의 핵심 비즈니스 로직을 조율합니다.
 */

/**
 * @brief 네트워크 통신 모듈 - SSL/TLS 기반의 안전한 소켓 통신
 * 
 * @details
 * 이 모듈은 클라이언트-서버 간의 안전한 통신을 위한 핵심 기능을 제공합니다:
 * 
 * 1. **SSL/TLS 보안 통신**: TLS 1.2/1.3 기반의 암호화된 통신
 * 2. **소켓 관리**: 서버/클라이언트 소켓 초기화 및 옵션 설정
 * 3. **메시지 전송**: 구조화된 메시지의 안정적인 전송/수신
 * 4. **에러 처리**: 네트워크 오류의 체계적 처리 및 복구
 * 
 * @note 모든 네트워크 통신은 SSL/TLS로 암호화되며, 연결 지속성과
 *       안정성을 위한 다양한 소켓 옵션이 적용됩니다.
 */

#include "../include/message.h"
#include "../include/network.h"
#include "../include/session.h"
#include "../include/ui.h"
#include "../include/resource.h"
#include "../include/reservation.h" 


/**
 * @brief 클라이언트 연결 정보를 관리하는 구조체
 * @details
 * 각 클라이언트 연결에 대한 모든 정보를 포함합니다:
 * - 네트워크 연결 정보 (소켓, SSL)
 * - 인증 상태 및 사용자 정보
 * - 마지막 활동 시간 (타임아웃 처리용)
 */
typedef struct {
    int socket_fd;                    ///< 클라이언트 소켓 파일 디스크립터
    SSL* ssl;                         ///< SSL 연결 객체
    ssl_handler_t* ssl_handler;       ///< SSL 핸들러
    char ip[INET_ADDRSTRLEN];         ///< 클라이언트 IP 주소
    session_state_t state;            ///< 세션 상태 (로그인 여부 등)
    char username[MAX_USERNAME_LENGTH]; ///< 인증된 사용자명
    time_t last_activity;             ///< 마지막 활동 시간 (타임아웃 처리용)
} Client;

// 함수 프로토타입
static int server_init(int port);
static void server_cleanup(void);
static void server_signal_handler(int signum);
static void* server_client_thread_func(void* arg);
static int server_handle_client_message(Client* client, const message_t* message);
static int server_handle_status_request(Client* client, const message_t* message);
static int server_handle_reserve_request(Client* client, const message_t* message);
static int server_handle_login_request(Client* client, const message_t* message);
static int server_handle_cancel_request(Client* client, const message_t* message);
static int server_send_error_response_with_code(SSL* ssl, error_code_t error_code, const char* error_message);
static int server_send_generic_response(Client* client, message_type_t type, const char* data, int arg_count, ...);
static void server_client_message_loop(Client* client);
static void server_cleanup_client(Client* client);
static void server_add_client_to_list(Client* client);
static void server_remove_client_from_list(Client* client);
static void server_broadcast_status_update(void);
static bool server_is_user_authenticated(const char* username, const char* password);
static void server_load_users_from_file(const char* filename);
static int server_process_device_reservation(Client* client, const char* device_id, int duration_sec);
static void server_trigger_ui_refresh(void);

// 전역 변수
static int server_sock;                    ///< 서버 소켓 파일 디스크립터
static bool running = true;                ///< 서버 실행 상태
static int self_pipe[2];                   ///< 시그널 처리를 위한 파이프
static ssl_manager_t ssl_manager;          ///< SSL 관리자
static resource_manager_t* resource_manager = NULL;      ///< 리소스 관리자
static reservation_manager_t* reservation_manager = NULL; ///< 예약 관리자
static session_manager_t* session_manager = NULL;        ///< 세션 관리자
static hash_table_t* user_credentials = NULL;            ///< 사용자 인증 정보 해시 테이블
static Client* client_list[MAX_CLIENTS];    ///< 연결된 클라이언트 목록
static int client_count = 0;               ///< 현재 연결된 클라이언트 수
static pthread_mutex_t client_list_mutex;  ///< 클라이언트 목록 보호용 뮤텍스
static int g_server_port = 0;              ///< 서버 포트 번호
static performance_stats_t g_perf_stats;   ///< 성능 통계

/**
 * @brief 서버 메인 함수
 * @details
 * 서버 애플리케이션의 진입점으로 다음 기능을 수행합니다:
 * 1. 명령행 인자 검증 및 포트 설정
 * 2. 서버 초기화 (소켓, SSL, 관리자 객체들)
 * 3. poll() 기반 이벤트 루프로 클라이언트 연결 처리
 * 4. 각 클라이언트를 별도 스레드에서 처리
 * 5. 실시간 UI 업데이트
 * 
 * @param argc 명령행 인자 개수
 * @param argv 명령행 인자 배열
 * @return 성공 시 0, 실패 시 1
 */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Server", "사용법: %s <포트>", argv[0]);
        return 1;
    }
    
    g_server_port = atoi(argv[1]);
    if (server_init(g_server_port) != 0) {
        utils_report_error(ERROR_NETWORK_SOCKET_CREATION_FAILED, "Main", "서버 초기화 실패");
        server_cleanup();
        return 1;
    }
    
    // 메인 이벤트 루프
    while (running) {
        struct pollfd fds[2];
        fds[0].fd = server_sock;      // 클라이언트 연결 요청
        fds[0].events = POLLIN;
        fds[1].fd = self_pipe[0];     // 시그널 처리
        fds[1].events = POLLIN;
        
        // 1초 타임아웃으로 poll 호출 (UI 업데이트 주기)
        int ret = poll(fds, 2, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Main", "Poll 에러: %s", strerror(errno));
            break;
        }
        
        // 시그널 처리
        if (fds[1].revents & POLLIN) {
            char buf[1];
            read(self_pipe[0], buf, 1);
            if (buf[0] == 's') {  // shutdown 시그널
                running = false;
                continue;
            }
            // 'u' 또는 다른 신호는 UI 갱신으로 간주
        }
        
        // 클라이언트 연결 처리
        if (fds[0].revents & POLLIN) {
            Client* client = (Client*)malloc(sizeof(Client));
            if (!client) { continue; }
            memset(client, 0, sizeof(Client));
            
            // SSL 핸드셰이크를 포함한 클라이언트 연결 수락
            client->ssl_handler = network_accept_client(server_sock, &ssl_manager, client->ip);
            if (!client->ssl_handler) {
                free(client);
                continue;
            }
            
            client->ssl = client->ssl_handler->ssl;
            client->socket_fd = client->ssl_handler->socket_fd;
            client->last_activity = time(NULL);
            
            // 클라이언트를 별도 스레드에서 처리
            pthread_t thread;
            if (pthread_create(&thread, NULL, server_client_thread_func, client) != 0) {
                utils_report_error(ERROR_SESSION_CREATION_FAILED, "Main", "클라이언트 스레드 생성 실패");
                server_cleanup_client(client);
            }
            pthread_detach(thread);
        }
        
        // UI 상태 업데이트
        server_draw_ui_for_current_state();
    }
    
    server_cleanup();
    return 0;
}

/**
 * @brief 모든 로그인된 클라이언트에게 장비 상태 업데이트 브로드캐스트
 * @details
 * 장비 상태가 변경되었을 때 모든 로그인된 클라이언트에게 실시간으로
 * 업데이트를 전송합니다. 이를 통해 클라이언트는 서버에 별도 요청 없이
 * 최신 상태를 받을 수 있습니다.
 */
static void server_broadcast_status_update(void) {
    LOG_INFO("Server", "상태 업데이트 브로드캐스트 시작: 연결된 클라이언트 수=%d", client_count);
    
    // 현재 장비 목록 및 상태 조회
    device_t devices[MAX_DEVICES];
    int count = resource_get_device_list(resource_manager, devices, MAX_DEVICES);
    if (count < 0) {
        LOG_ERROR("Server", "상태 업데이트 실패: 장비 목록 가져오기 실패");
        return;
    }
    
    // 상태 업데이트 메시지 생성
    message_t* status_msg = message_create(MSG_STATUS_UPDATE, NULL);
    if (!status_msg) {
        LOG_ERROR("Server", "상태 업데이트 실패: 상태 메시지 생성 실패");
        return;
    }
    
    if (!message_fill_status_response_args(status_msg, devices, count, resource_manager, reservation_manager)) {
        LOG_ERROR("Server", "상태 업데이트 실패: 상태 메시지 인수 채우기 실패");
        message_destroy(status_msg);
        return;
    }
    
    // 모든 로그인된 클라이언트에게 브로드캐스트
    pthread_mutex_lock(&client_list_mutex);
    int sent_count = 0;
    for (int i = 0; i < client_count; i++) {
        if (client_list[i] && client_list[i]->state == SESSION_LOGGED_IN) {
            if (network_send_message(client_list[i]->ssl, status_msg) >= 0) {
                sent_count++;
                LOG_DEBUG("Server", "상태 업데이트 전송 성공: 클라이언트=%s", client_list[i]->username);
            } else {
                LOG_WARNING("Server", "상태 업데이트 전송 실패: 클라이언트=%s", client_list[i]->username);
            }
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
    
    LOG_INFO("Server", "상태 업데이트 브로드캐스트 완료: 전송된 클라이언트=%d/%d", sent_count, client_count);
    message_destroy(status_msg);
}

/**
 * @brief 클라이언트 메시지 처리 루프
 * @details
 * 각 클라이언트 스레드에서 실행되며, 클라이언트로부터 메시지를 지속적으로
 * 수신하고 처리합니다. 성능 통계도 함께 수집합니다.
 * 
 * @param client 클라이언트 객체
 */
static void server_client_message_loop(Client* client) {
    while (running) {
        message_t* msg = message_receive(client->ssl);
        if (msg) {
            // 성능 측정 시작
            uint64_t start_time = utils_get_current_time();

            client->last_activity = time(NULL);
            int result = server_handle_client_message(client, msg);

            // 성능 측정 및 통계 업데이트
            uint64_t end_time = utils_get_current_time();
            uint64_t response_time = (end_time - start_time);

            pthread_mutex_lock(&g_perf_stats.mutex);
            g_perf_stats.total_requests++;
            if (result == 0) g_perf_stats.successful_requests++;
            else g_perf_stats.failed_requests++;
            g_perf_stats.total_response_time += response_time;
            if (g_perf_stats.max_response_time < response_time)
                g_perf_stats.max_response_time = response_time;
            if (g_perf_stats.min_response_time == 0 || g_perf_stats.min_response_time > response_time)
                g_perf_stats.min_response_time = response_time;
            pthread_mutex_unlock(&g_perf_stats.mutex);

            // UI 갱신 트리거
            server_trigger_ui_refresh();

            message_destroy(msg);
        } else {
            // 클라이언트 연결 종료
            break;
        }
    }
}

/**
 * @brief 클라이언트를 관리 목록에 추가
 * @details
 * 스레드 안전성을 위해 뮤텍스로 보호하여 클라이언트 목록에 추가합니다.
 * 
 * @param client 추가할 클라이언트 객체
 */
static void server_add_client_to_list(Client* client) {
    pthread_mutex_lock(&client_list_mutex);
    if (client_count < MAX_CLIENTS) client_list[client_count++] = client;
    pthread_mutex_unlock(&client_list_mutex);
    server_trigger_ui_refresh();
}

static void server_remove_client_from_list(Client* client) {
    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < client_count; i++) {
        if (client_list[i] == client) {
            client_list[i] = client_list[client_count - 1];
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
    server_trigger_ui_refresh();
}

static void server_cleanup_client(Client* client) {
    if (!client) return;
    if (client->state == SESSION_LOGGED_IN) session_close(session_manager, client->username);
    if (client->ssl_handler) network_cleanup_ssl_handler(client->ssl_handler);
    if (client->socket_fd >= 0) close(client->socket_fd);
    free(client);
}

static void* server_client_thread_func(void* arg) {
    Client* client = (Client*)arg;
    // SSL 핸들러와 핸드셰이크는 이미 main에서 network_accept_client()로 처리됨
    server_add_client_to_list(client);
    server_client_message_loop(client);
    server_remove_client_from_list(client);
    server_cleanup_client(client);
    return NULL;
}

static int server_process_device_reservation(Client* client, const char* device_id, int duration_sec) {
    LOG_INFO("Server", "예약 처리 검증 시작: 장비=%s, 사용자=%s", device_id, client->username);
    
    if (!resource_is_device_available(resource_manager, device_id)) {
        LOG_WARNING("Server", "예약 실패: 장비 사용 불가 (장비=%s, 사용자=%s)", device_id, client->username);
        reservation_t* active_res = reservation_get_active_for_device(reservation_manager, resource_manager, device_id);
        if (active_res) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "사용 불가: '%s'님이 사용 중입니다.", active_res->username);
            LOG_INFO("Server", "예약 실패: 다른 사용자가 사용 중 (장비=%s, 요청자=%s, 사용자=%s)", 
                    device_id, client->username, active_res->username);
            return server_send_error_response_with_code(client->ssl, ERROR_RESERVATION_ALREADY_EXISTS, error_msg);
        } else {
            LOG_WARNING("Server", "예약 실패: 장비 점검 중 또는 사용 불가 (장비=%s, 사용자=%s)", device_id, client->username);
            return server_send_error_response_with_code(client->ssl, ERROR_RESOURCE_IN_USE, "현재 사용 불가 또는 점검 중인 장비입니다.");
        }
    }
    
    LOG_INFO("Server", "예약 생성 시작: 장비=%s, 사용자=%s, 시간=%d초", device_id, client->username, duration_sec);
    time_t start = time(NULL);
    time_t end = start + duration_sec;
    uint32_t new_res_id = reservation_create(reservation_manager, device_id, client->username, start, end, "User Reservation");
    if (new_res_id == 0) {
        LOG_ERROR("Server", "예약 생성 실패: 장비=%s, 사용자=%s, 시간=%d초", device_id, client->username, duration_sec);
        return server_send_error_response_with_code(client->ssl, ERROR_UNKNOWN, "예약 생성에 실패했습니다 (시간 중복 등).");
    }
    
    LOG_INFO("Server", "예약 생성 성공: 예약ID=%u, 장비=%s, 사용자=%s, 시작=%ld, 종료=%ld", 
             new_res_id, device_id, client->username, start, end);
    
    LOG_INFO("Server", "상태 업데이트 브로드캐스트 시작");
    server_broadcast_status_update();
    LOG_INFO("Server", "상태 업데이트 브로드캐스트 완료");
    
    message_t* response = message_create(MSG_RESERVE_RESPONSE, "success");
    if (response) {
        device_t updated_device;
        device_t* devices_ptr = (device_t*)utils_hashtable_get(resource_manager->devices, device_id);
        if (devices_ptr) {
            memcpy(&updated_device, devices_ptr, sizeof(device_t));
            response->args[0] = strdup(device_id);
            response->arg_count = 1;
            message_fill_status_response_args(response, &updated_device, 1, resource_manager, reservation_manager);
        }
        
        LOG_INFO("Server", "예약 성공 응답 전송: 예약ID=%u, 장비=%s, 사용자=%s", new_res_id, device_id, client->username);
        network_send_message(client->ssl, response);
        message_destroy(response);
    } else {
        LOG_ERROR("Server", "예약 성공 응답 생성 실패: 예약ID=%u, 장비=%s, 사용자=%s", new_res_id, device_id, client->username);
    }
    
    LOG_INFO("Server", "예약 처리 완료: 예약ID=%u, 장비=%s, 사용자=%s", new_res_id, device_id, client->username);
    return 0;
}

static bool server_is_user_authenticated(const char* username, const char* password) {
    if (!user_credentials) {
        LOG_WARNING("Auth", "사용자 정보 해시 테이블이 초기화되지 않음");
        return false;
    }

    // 해시 테이블에서 사용자 이름으로 저장된 비밀번호를 조회
    char* stored_password = (char*)utils_hashtable_get(user_credentials, username);

    if (stored_password && strcmp(stored_password, password) == 0) {
        LOG_INFO("Auth", "사용자 인증 성공: %s", username);
        return true; // 비밀번호 일치
    }
    
    LOG_WARNING("Auth", "사용자 인증 실패: %s (사용자 없음 또는 비밀번호 불일치)", username);
    return false; // 사용자가 없거나 비밀번호 불일치
}

static int server_handle_login_request(Client* client, const message_t* message) {
    if (message->arg_count < 2) {
        LOG_WARNING("Auth", "로그인 요청 실패: 인수 부족 (필요: 2, 받음: %d)", message->arg_count);
        return server_send_error_response_with_code(client->ssl, ERROR_INVALID_PARAMETER, "로그인 정보가 부족합니다.");
    }
    const char* user = message->args[0];
    const char* pass = message->args[1];

    LOG_INFO("Auth", "로그인 시도: 사용자='%s', IP=%s", user, client->ip);

    // 1단계: 사용자 자격 증명 확인
    if (!server_is_user_authenticated(user, pass)) {
        LOG_WARNING("Auth", "로그인 실패: 사용자 '%s'의 자격 증명이 올바르지 않습니다. (IP: %s)", user, client->ip);
        return server_send_error_response_with_code(client->ssl, ERROR_SESSION_AUTHENTICATION_FAILED, NULL);
    }

    LOG_INFO("Auth", "사용자 자격 증명 확인 성공: '%s'", user);

    // 2단계: 세션 생성 (중복 로그인 방지)
    server_session_t* session = session_create(session_manager, user, client->ip, 0);
    if (!session) {
        // session_create는 사용자가 이미 존재할 경우 NULL을 반환합니다.
        LOG_WARNING("Auth", "로그인 실패: 사용자 '%s'는 이미 로그인되어 있습니다. (IP: %s)", user, client->ip);
        return server_send_error_response_with_code(client->ssl, ERROR_SESSION_ALREADY_EXISTS, NULL);
    }

    // 3단계: 로그인 성공 처리
    client->state = SESSION_LOGGED_IN;
    strncpy(client->username, user, MAX_USERNAME_LENGTH - 1);
    client->username[MAX_USERNAME_LENGTH - 1] = '\0';
    
    LOG_INFO("Auth", "사용자 '%s'가 IP주소 %s 에서 로그인했습니다.", user, client->ip);

    // 4단계: 클라이언트에 성공 응답 전송
    server_send_generic_response(client, MSG_LOGIN, "success", 1, user);

    return 0;
}

static int server_handle_status_request(Client* client, const message_t* message) {
    (void)message; 
    device_t devices[MAX_DEVICES];
    int count = resource_get_device_list(resource_manager, devices, MAX_DEVICES);
    if (count < 0) {
        return server_send_error_response_with_code(client->ssl, ERROR_UNKNOWN, "서버에서 장비 목록을 가져오는 데 실패했습니다.");
    }
    // LOG_INFO("Server", "장비 목록 요청 수신, 장비 개수: %d", count);
    message_t* response = message_create(MSG_STATUS_RESPONSE, NULL);
    if (response) {
        if (message_fill_status_response_args(response, devices, count, resource_manager, reservation_manager)) {
            network_send_message(client->ssl, response);
            // LOG_INFO("Server", "MSG_STATUS_RESPONSE 전송 완료");
        } else {
            // LOG_WARNING("Server", "MSG_STATUS_RESPONSE 인자 설정 실패");
        }
        message_destroy(response);
    } else {
        // LOG_WARNING("Server", "MSG_STATUS_RESPONSE 생성 실패");
    }
    return 0;
}

static int server_handle_reserve_request(Client* client, const message_t* message) {
    if (message->arg_count < 2) {
        LOG_WARNING("Server", "예약 요청 실패: 인수 부족 (필요: 2, 받음: %d)", message->arg_count);
        return server_send_error_response_with_code(client->ssl, ERROR_INVALID_PARAMETER, "예약 요청 정보(장비 ID, 시간)가 부족합니다.");
    }
    const char* device_id = message->args[0];
    int duration_sec = atoi(message->args[1]);
    
    LOG_INFO("Server", "예약 요청 수신: 사용자=%s, 장비=%s, 시간=%d초", client->username, device_id, duration_sec);
    
    if (duration_sec <= 0) {
        LOG_WARNING("Server", "예약 요청 실패: 유효하지 않은 시간 (사용자=%s, 장비=%s, 시간=%d초)", 
                   client->username, device_id, duration_sec);
        return server_send_error_response_with_code(client->ssl, ERROR_RESERVATION_INVALID_TIME, "유효하지 않은 예약 시간입니다.");
    }
    
    LOG_INFO("Server", "예약 처리 시작: 사용자=%s, 장비=%s, 시간=%d초", client->username, device_id, duration_sec);
    server_process_device_reservation(client, device_id, duration_sec);
    return 0;
}

static int server_handle_cancel_request(Client* client, const message_t* message) {
    if (message->arg_count < 1) {
        return server_send_error_response_with_code(client->ssl, ERROR_INVALID_PARAMETER, "예약 취소 정보(장비 ID)가 부족합니다.");
    }

    const char* device_id = message->args[0];
    reservation_t* res = reservation_get_active_for_device(reservation_manager, resource_manager, device_id);

    if (!res) {
        return server_send_error_response_with_code(client->ssl, ERROR_RESERVATION_NOT_FOUND, "취소할 수 있는 예약이 없습니다.");
    }
    if (strcmp(res->username, client->username) != 0) {
        return server_send_error_response_with_code(client->ssl, ERROR_RESERVATION_PERMISSION_DENIED, "본인의 예약이 아니므로 취소할 수 없습니다.");
    }

    // [수정된 로직] 예약 취소 함수만 호출
    if (reservation_cancel(reservation_manager, res->id, client->username)) {
        // 성공 시 브로드캐스트와 응답 전송
        server_broadcast_status_update();
        server_send_generic_response(client, MSG_CANCEL_RESPONSE, "success", 0);
    } else {
        // 실패 응답
        server_send_error_response_with_code(client->ssl, ERROR_UNKNOWN, "알 수 없는 오류로 예약 취소에 실패했습니다.");
    }

    return 0;
}

static int server_handle_client_message(Client* client, const message_t* message) {
    if (!client || !message) return -1;
    // 로그인되지 않은 상태에서는 로그인 요청만 허용
    if (client->state != SESSION_LOGGED_IN && message->type != MSG_LOGIN) {
        return server_send_error_response_with_code(client->ssl, ERROR_PERMISSION_DENIED, "로그인이 필요한 서비스입니다.");
    }
    switch (message->type) {
        case MSG_LOGIN: 
            return server_handle_login_request(client, message); // 리팩토링된 함수 호출
        case MSG_STATUS_REQUEST: 
            return server_handle_status_request(client, message);
        case MSG_RESERVE_REQUEST: 
            return server_handle_reserve_request(client, message);
        case MSG_CANCEL_REQUEST: 
            return server_handle_cancel_request(client, message);
        case MSG_TIME_SYNC_REQUEST:
        {
            // 클라이언트가 보낸 T1 타임스탬프가 있는지 확인
            if (message->arg_count < 1) {
                // 이전 버전 클라이언트와의 호환성을 위해 에러 대신 단순 처리
                return server_send_error_response_with_code(client->ssl, ERROR_INVALID_PARAMETER, "Invalid time sync request.");
            }

            // T1: 클라이언트가 보낸 타임스탬프
            const char* t1_str = message->args[0]; 

            // T3: 서버가 응답을 보내는 시간
            char t3_str[32];
            snprintf(t3_str, sizeof(t3_str), "%ld", time(NULL));

            // [수정] 클라이언트가 보낸 T1과 서버의 T3를 함께 응답으로 전송
            server_send_generic_response(client, MSG_TIME_SYNC_RESPONSE, "sync", 2, t1_str, t3_str);
            return 0;
        }
        case MSG_LOGOUT:
            LOG_INFO("Server", "클라이언트 로그아웃 요청 수신: %s", client->username);
            if (client->state == SESSION_LOGGED_IN) {
                session_close(session_manager, client->username);
                client->state = SESSION_DISCONNECTED;
                memset(client->username, 0, sizeof(client->username));
                server_send_generic_response(client, MSG_LOGOUT, "success", 0);
            }
            return 0;
        default: 
            return server_send_error_response_with_code(client->ssl, ERROR_INVALID_PARAMETER, "알 수 없거나 처리할 수 없는 요청입니다.");
    }
}

static void server_signal_handler(int signum) {
    utils_default_signal_handler(signum, self_pipe[1]);
}

static int server_init(int port) {
    if (pipe(self_pipe) == -1) { 
        utils_report_error(ERROR_FILE_OPERATION_FAILED, "Server", "pipe 생성 실패"); 
        return -1; 
    }
    signal(SIGINT, server_signal_handler);
    signal(SIGTERM, server_signal_handler);
    if (utils_init_logger("logs/server.log") < 0) return -1;
    if (network_init_ssl_manager(&ssl_manager, true, "certs/server.crt", "certs/server.key") < 0) return -1;
    if (ui_init(UI_SERVER) < 0) return -1;
    
    resource_manager = resource_init_manager();
    reservation_manager = reservation_init_manager(resource_manager, server_broadcast_status_update);
    session_manager = session_init_manager();
    server_load_users_from_file("users.txt"); // 사용자 정보 로드 추가
    
    if (!resource_manager || !reservation_manager || !session_manager || !user_credentials) return -1;
    
    server_sock = network_init_server_socket(port);
    if (server_sock < 0) return -1;

    // 성능 통계 뮤텍스 초기화
    pthread_mutex_init(&g_perf_stats.mutex, NULL);
    g_perf_stats.min_response_time = 0;
    g_perf_stats.max_response_time = 0;
    g_perf_stats.total_response_time = 0;
    g_perf_stats.total_requests = 0;
    g_perf_stats.successful_requests = 0;
    g_perf_stats.failed_requests = 0;
    g_perf_stats.max_concurrent_requests = 0;
    g_perf_stats.total_data_sent = 0;
    g_perf_stats.total_data_received = 0;
    g_perf_stats.total_errors = 0;
    
    return 0;
}

static void server_cleanup(void) {
    running = false;
    
    if (user_credentials) {
        utils_hashtable_destroy(user_credentials);
        user_credentials = NULL;
        LOG_INFO("Auth", "사용자 정보 해시 테이블 정리 완료");
    }
    
    if (session_manager) {
        session_cleanup_manager(session_manager);
        session_manager = NULL;
    }
    
    if (reservation_manager) {
        reservation_cleanup_manager(reservation_manager);
        reservation_manager = NULL;
    }
    
    if (resource_manager) {
        resource_cleanup_manager(resource_manager);
        resource_manager = NULL;
    }
    
    ui_cleanup();
    network_cleanup_ssl_manager(&ssl_manager);
    utils_cleanup_logger();
    
    if (server_sock >= 0) {
        close(server_sock);
        server_sock = -1;
    }
    
    close(self_pipe[0]);
    close(self_pipe[1]);

    // 성능 통계 출력 및 뮤텍스 해제
    utils_print_performance_stats(&g_perf_stats);
    pthread_mutex_destroy(&g_perf_stats.mutex);
}

/**
 * @brief 클라이언트에 특정 타입의 응답 메시지를 전송하는 제네릭 함수 (리팩토링)
 * @details 메시지 생성, 인자 추가, 전송, 메모리 해제 과정을 한번에 처리합니다.
 * @param client 클라이언트 객체
 * @param type 전송할 메시지 타입
 * @param data 메시지의 data 필드에 들어갈 문자열 (예: "success", "fail")
 * @param arg_count 전달할 인자의 개수
 * @param ... 전달할 가변 인자 (const char* 타입)
 * @return 성공 시 0, 실패 시 -1
 */
static int server_send_generic_response(Client* client, message_type_t type, const char* data, int arg_count, ...) {
    if (!client || !client->ssl) return -1;

    message_t* response = message_create(type, data);
    if (!response) {
        // LOG_ERROR("Response", "응답 메시지 생성 실패 (type: %d)", type);
        return -1;
    }

    va_list args;
    va_start(args, arg_count);
    for (int i = 0; i < arg_count; i++) {
        const char* arg = va_arg(args, const char*);
        if (arg) {
            response->args[i] = strdup(arg);
            if (!response->args[i]) {
                // LOG_ERROR("Response", "응답 인자 메모리 복사 실패");
                response->arg_count = i; 
                message_destroy(response);
                va_end(args);
                return -1;
            }
        }
    }
    va_end(args);
    response->arg_count = arg_count;

    int ret = network_send_message(client->ssl, response);
    message_destroy(response);

    return ret;
}

static void server_load_users_from_file(const char* filename) {
    user_credentials = utils_hashtable_create(MAX_CLIENTS, free); // 비밀번호 문자열을 free로 해제
    if (!user_credentials) {
        utils_report_error(ERROR_HASHTABLE_CREATION_FAILED, "Auth", "사용자 정보 해시 테이블 생성 실패");
        return;
    }

    FILE* fp = fopen(filename, "r");
    if (!fp) {
        utils_report_error(ERROR_FILE_OPERATION_FAILED, "Auth", "'%s' 파일을 열 수 없습니다.", filename);
        utils_hashtable_destroy(user_credentials);
        return;
    }

    char line[256];
    char username[MAX_USERNAME_LENGTH];
    char password[128];
    int user_count = 0;

    while (fgets(line, sizeof(line), fp)) {
        // 개행 문자 제거
        line[strcspn(line, "\n")] = 0;
        
        if (sscanf(line, "%[^:]:%s", username, password) == 2) {
            if (utils_hashtable_insert(user_credentials, username, strdup(password))) {
                LOG_INFO("Auth", "사용자 로드: %s", username);
                user_count++;
            } else {
                LOG_WARNING("Auth", "사용자 로드 실패: %s", username);
            }
        }
    }
    
    fclose(fp);
    LOG_INFO("Auth", "사용자 정보 로드 완료: 총 %d명", user_count);
}

void server_draw_ui_for_current_state(void) {
    if (!g_ui_manager) return;
    pthread_mutex_lock(&g_ui_manager->mutex);

    // [주석처리] UI 갱신 시작 로그
    // LOG_INFO("ServerUI", "UI 갱신 시작");

    // 1. 상단 정보 바 (status_win) - 성능 통계 및 에러 메시지 공간
    werase(g_ui_manager->status_win);
    box(g_ui_manager->status_win, 0, 0);
    int session_count = (session_manager && session_manager->sessions) ? session_manager->sessions->count : 0;
    
    // 성능 통계 정보 가져오기
    pthread_mutex_lock(&g_perf_stats.mutex);
    uint64_t total_req = g_perf_stats.total_requests;
    uint64_t success_req = g_perf_stats.successful_requests;
    uint64_t failed_req = g_perf_stats.failed_requests;
    uint64_t avg_response_time = (total_req > 0) ? (g_perf_stats.total_response_time / total_req) : 0;
    uint64_t max_response_time = g_perf_stats.max_response_time;
    uint64_t min_response_time = g_perf_stats.min_response_time;
    pthread_mutex_unlock(&g_perf_stats.mutex);
    
    // 첫 번째 줄: 기본 서버 정보 (시간 제거) + 에러 메시지 공간
    mvwprintw(g_ui_manager->status_win, 1, 2, "포트: %d  세션: %d", g_server_port, session_count);
    
    // 두 번째 줄: 성능 통계
    mvwprintw(g_ui_manager->status_win, 2, 2, "요청: 총%lu 성공%lu 실패%lu | 응답시간: 평균%luμs 최대%luμs 최소%luμs", 
              total_req, success_req, failed_req, avg_response_time, max_response_time, min_response_time);
    
    wrefresh(g_ui_manager->status_win);

    // 2. 장비 목록 표 (menu_win) - status_win이 4줄이므로 menu_win 위치 조정
    werase(g_ui_manager->menu_win);
    box(g_ui_manager->menu_win, 0, 0);
    
    // 장비 목록
    device_t devices[MAX_DEVICES];
    int count = resource_get_device_list(resource_manager, devices, MAX_DEVICES);
    
    // [주석처리] 장비 목록 조회 로그
    // LOG_INFO("ServerUI", "장비 목록 조회: 총 %d개 장비", count);
    
    // 공통 장비 목록 테이블 그리기 함수 사용
    ui_draw_device_table(g_ui_manager->menu_win, devices, count, -1, true, 
                        reservation_manager, resource_manager, 0, true);
    
    wrefresh(g_ui_manager->menu_win);

    // 3. 하단 안내/상태 바 (message_win)
    werase(g_ui_manager->message_win);
    box(g_ui_manager->message_win, 0, 0);
    mvwprintw(g_ui_manager->message_win, 0, 2, "[ESC] 종료   [↑↓] 스크롤   상태: 서버 정상 동작 중");
    wrefresh(g_ui_manager->message_win);

    pthread_mutex_unlock(&g_ui_manager->mutex);
    
    // [주석처리] UI 갱신 완료 로그
    // LOG_INFO("ServerUI", "UI 갱신 완료");
}

// [추가] UI 갱신 트리거 함수
static void server_trigger_ui_refresh(void) {
    // [주석처리] UI 갱신 트리거 로그
    // LOG_INFO("ServerUI", "UI 갱신 트리거 발생");
    
    // 파이프에 간단한 데이터를 써서 poll()을 깨운다.
    // 에러 처리는 간단하게 처리하거나 무시할 수 있다.
    int result = write(self_pipe[1], "u", 1); // 'u' for update
    if (result < 0) {
        LOG_WARNING("ServerUI", "UI 갱신 트리거 실패: %s", strerror(errno));
    } else {
        // [주석처리] UI 갱신 트리거 성공 로그
        // LOG_INFO("ServerUI", "UI 갱신 트리거 성공");
    }
}

static int server_send_error_response_with_code(SSL* ssl, error_code_t error_code, const char* error_message) {
    if (!ssl) return -1;
    const char* msg = error_message ? error_message : message_get_error_string(error_code);
    message_t* response = message_create(MSG_ERROR, msg);
    if (!response) return -1;
    response->error_code = error_code;
    int ret = network_send_message(ssl, response);
    message_destroy(response);
    return ret;
}
