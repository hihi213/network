#include "../include/logger.h"
#include "../include/message.h"
#include "../include/network.h"
#include "../include/session.h"
#include "../include/ui.h"
#include "../include/performance.h"
#include "../include/resource.h"
#include "../include/reservation.h"

// Client 구조체 정의 (가장 위에 위치)
/* --- 클라이언트 정보 구조체 --- */
// 서버에서 각 클라이언트를 관리하기 위한 내부 구조체
typedef struct {
    int socket_fd;
    SSL* ssl;
    SSLHandler* ssl_handler;
    char ip[INET_ADDRSTRLEN];
    SessionState state;
    char username[MAX_USERNAME_LENGTH];
    time_t last_activity;
} Client;
/* --- 함수 프로토타입 --- */
static int init_server(int port);
static void cleanup_server(void);
static void signal_handler(int signum);
static void* client_thread_func(void* arg);
static int handle_client_message(Client* client, const Message* message);
static int handle_status_request(Client* client, const Message* message);
static int handle_reserve_request(Client* client, const Message* message);
static int handle_login_request(Client* client, const Message* message);
static int send_error_response(SSL* ssl, const char* error_message);

/* --- 전역 변수 --- */
static int server_sock;
static bool running = true;
static int self_pipe[2];

static SSLManager ssl_manager;
static ResourceManager* resource_manager = NULL;
static ReservationManager* reservation_manager = NULL;
static SessionManager* session_manager = NULL;


/* --- 함수 정의 --- */

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "사용법: %s <포트>\n", argv[0]);
        return 1;
    }

    if (init_server(atoi(argv[1])) != 0) {
        LOG_ERROR("Main", "서버 초기화 실패");
        cleanup_server();
        return 1;
    }

    LOG_INFO("Main", "서버가 클라이언트 연결을 기다립니다...");
    while (running) {
        struct pollfd fds[2];
        fds[0].fd = server_sock; // 클라이언트 연결 감지
        fds[0].events = POLLIN;
        fds[1].fd = self_pipe[0]; // 종료 신호 감지
        fds[1].events = POLLIN;

        int ret = poll(fds, 2, 1000); // 1초 타임아웃
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("Main", "Poll 에러: %s", strerror(errno));
            break;
        }
        if (ret == 0) { // 타임아웃 시 UI 업데이트 등 주기적 작업 수행
            Device devices[MAX_DEVICES];
            int count = get_device_list(resource_manager, devices, MAX_DEVICES);
            if (count >= 0) update_server_devices(devices, count);
            update_server_status(session_manager->session_count, atoi(argv[1]));
            refresh_all_windows();
            continue;
        }

        if (fds[1].revents & POLLIN) { // 종료 신호 수신
            running = false;
            continue;
        }
        
        if (fds[0].revents & POLLIN) { // 새 클라이언트 연결
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);

            if (client_sock < 0) { continue; }

            Client* client = (Client*)malloc(sizeof(Client));
            if (!client) { close(client_sock); continue; }
            
            memset(client, 0, sizeof(Client));
            client->socket_fd = client_sock;
            inet_ntop(AF_INET, &client_addr.sin_addr, client->ip, sizeof(client->ip));

            pthread_t thread;
            if (pthread_create(&thread, NULL, client_thread_func, client) != 0) {
                LOG_ERROR("Main", "클라이언트 스레드 생성 실패");
                close(client_sock);
                free(client);
            }
            pthread_detach(thread);
        }
    }

    cleanup_server();
    return 0;
}

static void* client_thread_func(void* arg) {
    Client* client = (Client*)arg;
    
    // [핵심] SSL 핸드셰이크는 이제 각 스레드에서 수행
    client->ssl_handler = create_ssl_handler(&ssl_manager, client->socket_fd);
    if (!client->ssl_handler || handle_ssl_handshake(client->ssl_handler) != 0) {
        LOG_ERROR("Thread", "SSL 핸드셰이크 실패: %s", client->ip);
        if(client->ssl_handler) cleanup_ssl_handler(client->ssl_handler);
        close(client->socket_fd);
        free(client);
        return NULL;
    }
    client->ssl = client->ssl_handler->ssl;
    client->last_activity = time(NULL);
    LOG_INFO("Thread", "클라이언트 스레드 시작 및 핸드셰이크 성공: %s", client->ip);

    while (running) {
        // [수정 1] message.c의 receive_message는 Message 포인터를 반환하므로, 그에 맞게 수정합니다.
        Message* msg = receive_message(client->ssl);
        
        if (msg) {
            handle_client_message(client, msg);
            cleanup_message(msg);
            free(msg); // receive_message가 동적 할당한 메모리를 해제합니다.
            client->last_activity = time(NULL);
        } else {
            // 수신 실패는 연결 종료로 간주합니다.
            break;
        }

        if (time(NULL) - client->last_activity > SESSION_TIMEOUT) {
            LOG_WARNING("Thread", "클라이언트 타임아웃: %s", client->ip);
            send_error_response(client->ssl, "세션 시간 초과");
            break;
        }
    }

    LOG_INFO("Thread", "클라이언트 연결 종료: %s", client->ip);
    if (client->state == SESSION_LOGGED_IN) close_session(session_manager, client->username);
    cleanup_ssl_handler(client->ssl_handler);
    close(client->socket_fd);
    free(client);
    return NULL;
}

static int handle_client_message(Client* client, const Message* message) {
    if (!client || !message) return -1;

    if (client->state != SESSION_LOGGED_IN && message->type != MSG_LOGIN) {
        return send_error_response(client->ssl, "로그인이 필요한 서비스입니다.");
    }

    switch (message->type) {
        case MSG_LOGIN:
            return handle_login_request(client, message);
        case MSG_STATUS_REQUEST:
            return handle_status_request(client, message);
        case MSG_RESERVE_REQUEST:
            return handle_reserve_request(client, message);
        default:
            LOG_ERROR("Handler", "알 수 없는 메시지 타입: %d", message->type);
            return send_error_response(client->ssl, "알 수 없는 요청입니다.");
    }
}
/**
 * @brief 클라이언트에게 에러 응답 메시지를 전송하는 헬퍼 함수.
 * @param ssl 에러를 보낼 클라이언트의 SSL 연결 객체.
 * @param error_message 전송할 에러 메시지 문자열.
 * @return 성공 시 0, 실패 시 음수를 반환.
 */

static int handle_login_request(Client* client, const Message* message) {
    if (message->arg_count < 2) return send_error_response(client->ssl, "로그인 정보 부족");

    const char* user = message->args[0];
    const char* pass = message->args[1];

    // 여기에 실제 DB나 파일에서 사용자 정보를 확인하는 인증 로직이 들어가야 합니다.
    // 지금은 임시로 "test" / "1234" 만 허용합니다.
    if (strcmp(user, "test") == 0 && strcmp(pass, "1234") == 0) {
        client->state = SESSION_LOGGED_IN;
        strncpy(client->username, user, MAX_USERNAME_LENGTH -1);
        create_session(session_manager, user, client->ip, 0); // 세션 생성
        Message* response = create_message(MSG_LOGIN, "success");
        send_message(client->ssl, response);
        cleanup_message(response);
        LOG_INFO("Auth", "로그인 성공: %s", user);
        return 0;
    } else {
        LOG_WARNING("Auth", "로그인 실패: %s", user);
        return send_error_response(client->ssl, "아이디 또는 비밀번호가 틀립니다.");
    }
}

static int handle_status_request(Client* client, const Message* message) {
    // 이 함수는 message 파라미터를 사용하지 않으므로 경고를 방지합니다.
    (void)message; 

    // 1. resource_manager에서 장비 목록을 가져옵니다.
    Device devices[MAX_DEVICES];
    int count = get_device_list(resource_manager, devices, MAX_DEVICES);
    if (count < 0) {
        LOG_ERROR("Handler", "장비 목록 조회 실패");
        return send_error_response(client->ssl, "서버에서 장비 목록을 가져오는 데 실패했습니다.");
    }

    // 2. 가져온 장비 목록으로 응답 메시지를 생성합니다.
    Message* response = create_status_response_message(devices, count);
    if (!response) {
        LOG_ERROR("Handler", "상태 응답 메시지 생성 실패");
        return send_error_response(client->ssl, "응답 메시지를 생성하는 데 실패했습니다.");
    }

    // 3. 생성한 메시지를 클라이언트에게 전송합니다.
    send_message(client->ssl, response);

    // 4. 메시지 생성 시 할당된 메모리를 정리합니다.
    cleanup_message(response);
    free(response); // Message 구조체 자체 메모리 해제

    LOG_INFO("Handler", "장비 상태 요청 처리 완료, %d개 장비 정보 전송", count);
    return 0;
}

static int handle_reserve_request(Client* client, const Message* message) {
    if (message->arg_count < 1) return send_error_response(client->ssl, "인자 부족");

    const char* device_id = message->args[0];

    if (!is_device_available(resource_manager, device_id)) {
        Reservation* active_res = get_active_reservation_for_device(reservation_manager, device_id);
        char error_msg[256];
        if (active_res) snprintf(error_msg, sizeof(error_msg), "사용 불가: '%s'님이 사용 중", active_res->username);
        else strncpy(error_msg, "현재 사용 불가 상태입니다.", sizeof(error_msg)-1);
        return send_error_response(client->ssl, error_msg);
    }
    
    time_t start = time(NULL), end = start + 3600; // 1시간
    if (!create_reservation(reservation_manager, device_id, client->username, start, end, "User Reservation")) {
        return send_error_response(client->ssl, "예약 생성 실패");
    }
    
    update_device_status(resource_manager, device_id, DEVICE_RESERVED);

    Message* response = create_message(MSG_RESERVE_RESPONSE, "success");
    send_message(client->ssl, response);
    cleanup_message(response);
    LOG_INFO("Handler", "예약 성공: 장비=%s, 사용자=%s", device_id, client->username);
    return 0;
}

static void signal_handler(int signum) {
    (void)write(self_pipe[1], "s", 1);
}

static int init_server(int port) {
    if (pipe(self_pipe) == -1) { perror("pipe"); return -1; }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (init_logger("logs/server.log") < 0) return -1;
    if (init_ui() < 0) return -1;
    if (init_ssl_manager(&ssl_manager, true, "certs/server.crt", "certs/server.key") < 0) return -1;

    resource_manager = init_resource_manager();
    reservation_manager = init_reservation_manager();
    session_manager = init_session_manager();
    if (!resource_manager || !reservation_manager || !session_manager) return -1;
    
    server_sock = init_server_socket(port);
    if (server_sock < 0) return -1;
    
    LOG_INFO("Init", "서버 초기화 성공 (Port: %d)", port);
    return 0;
}

static void cleanup_server(void) {
    LOG_INFO("Server", "서버 정리 시작");
    running = false;
    
    if (session_manager) cleanup_session_manager(session_manager);
    if (reservation_manager) cleanup_reservation_manager(reservation_manager);
    if (resource_manager) cleanup_resource_manager(resource_manager);
    
    if (server_sock >= 0) close(server_sock);
    cleanup_ssl_manager(&ssl_manager);
    cleanup_ui();
    cleanup_logger();
    close(self_pipe[0]);
    close(self_pipe[1]);
    LOG_INFO("Server", "서버 정리 완료");
}

static int send_error_response(SSL* ssl, const char* error_message) {
    // 1. 파라미터 유효성 검사
    if (!ssl || !error_message) {
        LOG_ERROR("Server", "send_error_response: 잘못된 파라미터");
        return -1;
    }

    // 2. message.c의 헬퍼 함수를 사용하여 에러 메시지 객체 생성
    // create_error_message는 내부적으로 create_message(MSG_ERROR, error_message)를 호출합니다.
    Message* response = create_error_message(error_message);
    if (!response) {
        LOG_ERROR("Server", "에러 응답 메시지 생성 실패");
        return -1;
    }

    // 3. network.c의 send_message를 사용하여 메시지 전송
    int ret = send_message(ssl, response);
    if (ret < 0) {
        LOG_ERROR("Server", "에러 응답 메시지 전송 실패");
    }

    // 4. 메시지 객체 자원 정리
    // create_message에서 malloc으로 할당된 Message 구조체 자체를 해제합니다.
    // cleanup_message는 Message 구조체 내부의 동적 할당된 필드(예: args)를 해제합니다.
    cleanup_message(response);
    free(response); // Message 구조체 자체의 메모리 해제

    return ret;
}