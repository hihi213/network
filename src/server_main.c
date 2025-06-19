// server_main.c
#include "../include/message.h"
#include "../include/network.h"
#include "../include/session.h"
#include "../include/ui.h"
#include "../include/resource.h"
#include "../include/reservation.h" 

// Client 구조체 정의
typedef struct {
    int socket_fd;
    SSL* ssl;
    SSLHandler* ssl_handler;
    char ip[INET_ADDRSTRLEN];
    SessionState state;
    char username[MAX_USERNAME_LENGTH];
    time_t last_activity;
} Client;

// 함수 프로토타입
static int init_server(int port);
static void cleanup_server(void);
static void signal_handler(int signum);
static void* client_thread_func(void* arg);
static int handle_client_message(Client* client, const Message* message);
static int handle_status_request(Client* client, const Message* message);
static int handle_reserve_request(Client* client, const Message* message);
static int handle_login_request(Client* client, const Message* message);
static int send_error_response(SSL* ssl, const char* error_message);
static void client_message_loop(Client* client);
static void cleanup_client(Client* client);
static void add_client_to_list(Client* client);
static void remove_client_from_list(Client* client);
static void broadcast_status_update(void);

// 전역 변수
static int server_sock;
static bool running = true;
static int self_pipe[2];
static SSLManager ssl_manager;
static ResourceManager* resource_manager = NULL;
static ReservationManager* reservation_manager = NULL;
static SessionManager* session_manager = NULL;
static Client* client_list[MAX_CLIENTS];
static int num_clients = 0;
static pthread_mutex_t client_list_mutex;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        error_report(ERROR_INVALID_PARAMETER, "Server", "사용법: %s <포트>", argv[0]);
        return 1;
    }
    if (init_server(atoi(argv[1])) != 0) {
        error_report(ERROR_NETWORK_SOCKET_CREATION_FAILED, "Main", "서버 초기화 실패");
        cleanup_server();
        return 1;
    }
    LOG_INFO("Main", "서버가 클라이언트 연결을 기다립니다...");
    while (running) {
        struct pollfd fds[2];
        fds[0].fd = server_sock;
        fds[0].events = POLLIN;
        fds[1].fd = self_pipe[0];
        fds[1].events = POLLIN;
        int ret = poll(fds, 2, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            error_report(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Main", "Poll 에러: %s", strerror(errno));
            break;
        }
        if (ret == 0) {
            Device devices[MAX_DEVICES];
            int count = get_device_list(resource_manager, devices, MAX_DEVICES);
            if (count >= 0)  update_server_devices(devices, count, resource_manager, reservation_manager);
            update_server_status(session_manager->sessions->count, atoi(argv[1]));
            refresh_all_windows();
            continue;
        }
        if (fds[1].revents & POLLIN) {
            running = false;
            continue;
        }
        if (fds[0].revents & POLLIN) {
            Client* client = (Client*)malloc(sizeof(Client));
            if (!client) { continue; }
            memset(client, 0, sizeof(Client));
            
            // accept_client 함수를 사용하여 클라이언트 연결 처리
            client->ssl_handler = accept_client(server_sock, &ssl_manager, client->ip);
            if (!client->ssl_handler) {
                free(client);
                continue;
            }
            
            client->ssl = client->ssl_handler->ssl;
            client->socket_fd = client->ssl_handler->socket_fd;
            client->last_activity = time(NULL);
            
            pthread_t thread;
            if (pthread_create(&thread, NULL, client_thread_func, client) != 0) {
                error_report(ERROR_SESSION_CREATION_FAILED, "Main", "클라이언트 스레드 생성 실패");
                cleanup_client(client);
            }
            pthread_detach(thread);
        }
    }
    cleanup_server();
    return 0;
}

static void broadcast_status_update(void) {
    Device devices[MAX_DEVICES];
    int count = get_device_list(resource_manager, devices, MAX_DEVICES);
    if (count < 0) return;
    Message* response = create_message(MSG_STATUS_UPDATE, NULL);
    if (!response) return;
    if (!fill_status_response_args(response, devices, count, resource_manager, reservation_manager)) {
        cleanup_message(response);
        free(response);
        return;
    }
    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < num_clients; i++) {
        if (client_list[i] && client_list[i]->state == SESSION_LOGGED_IN) {
            send_message(client_list[i]->ssl, response);
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
    cleanup_message(response);
    free(response);
}

static void client_message_loop(Client* client) {
    while (running) {
        Message* msg = receive_message(client->ssl);
        if (msg) {
            handle_client_message(client, msg);
            cleanup_message(msg);
            free(msg);
            client->last_activity = time(NULL);
        } else {
            LOG_INFO("Thread", "클라이언트(%s)가 연결을 종료했습니다.", client->ip);
            break;
        }
    }
}

static void add_client_to_list(Client* client) {
    pthread_mutex_lock(&client_list_mutex);
    if (num_clients < MAX_CLIENTS) client_list[num_clients++] = client;
    pthread_mutex_unlock(&client_list_mutex);
}

static void remove_client_from_list(Client* client) {
    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < num_clients; i++) {
        if (client_list[i] == client) {
            client_list[i] = client_list[num_clients - 1];
            num_clients--;
            break;
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
}

static void cleanup_client(Client* client) {
    if (!client) return;
    if (client->state == SESSION_LOGGED_IN) close_session(session_manager, client->username);
    if (client->ssl_handler) cleanup_ssl_handler(client->ssl_handler);
    if (client->socket_fd >= 0) close(client->socket_fd);
    free(client);
}

static void* client_thread_func(void* arg) {
    Client* client = (Client*)arg;
    // SSL 핸들러와 핸드셰이크는 이미 main에서 accept_client()로 처리됨
    add_client_to_list(client);
    client_message_loop(client);
    remove_client_from_list(client);
    cleanup_client(client);
    return NULL;
}

static int process_device_reservation(Client* client, const char* device_id, int duration_sec) {
    if (!is_device_available(resource_manager, device_id)) {
        char error_msg[256];
        Reservation* active_res = get_active_reservation_for_device(reservation_manager, resource_manager, device_id);
        if (active_res) {
            snprintf(error_msg, sizeof(error_msg), "사용 불가: '%s'님이 사용 중입니다.", active_res->username);
        } else {
            strncpy(error_msg, "현재 사용 불가 또는 점검 중인 장비입니다.", sizeof(error_msg)-1);
        }
       return send_error_response(client->ssl, error_msg);
    }
    time_t start = time(NULL);
    time_t end = start + duration_sec;
    uint32_t new_res_id = create_reservation(reservation_manager, device_id, client->username, start, end, "User Reservation");
    if (new_res_id == 0) {
        return send_error_response(client->ssl, "예약 생성에 실패했습니다 (시간 중복 등).");
    }
    if (!update_device_status(resource_manager, device_id, DEVICE_RESERVED, new_res_id)) {
        cancel_reservation(reservation_manager, new_res_id, "system");
        return send_error_response(client->ssl, "서버 내부 오류: 예약 상태 동기화 실패");
    }
    broadcast_status_update();
    Message* response = create_message(MSG_RESERVE_RESPONSE, "success");
    if (response) {
        send_message(client->ssl, response);
        cleanup_message(response);
        free(response);
    }
    return 0;
}

static int handle_login_request(Client* client, const Message* message) {
    if (message->arg_count < 2) return send_error_response(client->ssl, "로그인 정보 부족");
    const char* user = message->args[0];
    const char* pass = message->args[1];
    if (strcmp(user, "test") == 0 && strcmp(pass, "1234") == 0) {
        client->state = SESSION_LOGGED_IN;
        strncpy(client->username, user, MAX_USERNAME_LENGTH - 1);
        client->username[MAX_USERNAME_LENGTH - 1] = '\0';
        create_session(session_manager, user, client->ip, 0);
        Message* response = create_message(MSG_LOGIN, "success");
        if (response) {
            response->args[0] = strdup(user);
            response->arg_count = 1;
            send_message(client->ssl, response);
            cleanup_message(response);
            free(response);
        }
        return 0;
    } else {
        return send_error_response(client->ssl, "아이디 또는 비밀번호가 틀립니다.");
    }
}

static int handle_status_request(Client* client, const Message* message) {
    (void)message; 
    Device devices[MAX_DEVICES];
    int count = get_device_list(resource_manager, devices, MAX_DEVICES);
    if (count < 0) return send_error_response(client->ssl, "서버에서 장비 목록을 가져오는 데 실패했습니다.");
    Message* response = create_status_response_message(devices, count, resource_manager, reservation_manager);
    if (!response) return send_error_response(client->ssl, "응답 메시지를 생성하는 데 실패했습니다.");
    send_message(client->ssl, response);
    cleanup_message(response);
    free(response);
    return 0;
}

static int handle_reserve_request(Client* client, const Message* message) {
    if (message->arg_count < 2) return send_error_response(client->ssl, "예약 요청 정보(장비 ID, 시간)가 부족합니다.");
    const char* device_id = message->args[0];
    int duration_sec = atoi(message->args[1]);
    if (duration_sec <= 0) return send_error_response(client->ssl, "유효하지 않은 예약 시간입니다.");
    process_device_reservation(client, device_id, duration_sec);
    return 0;
}

static int handle_client_message(Client* client, const Message* message) {
    if (!client || !message) return -1;
    if (client->state != SESSION_LOGGED_IN && message->type != MSG_LOGIN) {
        return send_error_response(client->ssl, "로그인이 필요한 서비스입니다.");
    }
    switch (message->type) {
        case MSG_LOGIN: return handle_login_request(client, message);
        case MSG_STATUS_REQUEST: return handle_status_request(client, message);
        case MSG_RESERVE_REQUEST: return handle_reserve_request(client, message);
        default: return send_error_response(client->ssl, "알 수 없거나 처리할 수 없는 요청입니다.");
    }
}

static void signal_handler(int signum) {
    (void)signum;
    if (pipe(self_pipe) == -1) { 
        error_report(ERROR_FILE_OPERATION_FAILED, "Server", "pipe 생성 실패"); 
        return; 
    }
    (void)write(self_pipe[1], "s", 1);
}

static int init_server(int port) {
    if (pipe(self_pipe) == -1) { 
        error_report(ERROR_FILE_OPERATION_FAILED, "Server", "pipe 생성 실패"); 
        return -1; 
    }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    if (init_logger("logs/server.log") < 0) return -1;
    if (init_ui() < 0) return -1;
    if (init_ssl_manager(&ssl_manager, true, "certs/server.crt", "certs/server.key") < 0) return -1;
    if (pthread_mutex_init(&client_list_mutex, NULL) != 0) return -1;
    resource_manager = init_resource_manager();
    reservation_manager = init_reservation_manager(resource_manager, broadcast_status_update);
    session_manager = init_session_manager();
    if (!resource_manager || !reservation_manager || !session_manager) return -1;
    server_sock = init_server_socket(port);
    if (server_sock < 0) return -1;
    return 0;
}

static void cleanup_server(void) {
    running = false;
    if (session_manager) cleanup_session_manager(session_manager);
    if (reservation_manager) cleanup_reservation_manager(reservation_manager);
    if (resource_manager) cleanup_resource_manager(resource_manager);
    if (server_sock >= 0) close(server_sock);
    cleanup_ssl_manager(&ssl_manager);
    cleanup_ui();
    cleanup_logger();
    pthread_mutex_destroy(&client_list_mutex);
    close(self_pipe[0]);
    close(self_pipe[1]);
}

static int send_error_response(SSL* ssl, const char* error_message) {
    if (!ssl || !error_message) return -1;
    Message* response = create_error_message(error_message);
    if (!response) return -1;
    int ret = send_message(ssl, response);
    cleanup_message(response);
    free(response);
    return ret;
}