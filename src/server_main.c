// server_main.c
#include "../include/message.h"
#include "../include/network.h"
#include "../include/session.h"
#include "../include/ui.h"
#include "../include/resource.h"
#include "../include/reservation.h" 

// 메시지 노드 구조체
typedef struct MsgNode {
    Message* msg;
    struct MsgNode* next;
} MsgNode;

// Client 구조체 정의
typedef struct {
    int socket_fd;
    SSL* ssl;
    SSLHandler* ssl_handler;
    char ip[INET_ADDRSTRLEN];
    SessionState state;
    char username[MAX_USERNAME_LENGTH];
    time_t last_activity;
    MsgNode* queues[11]; // 우선순위 0~10 (MAX_PRIORITY=10 가정)
    MsgNode* tails[11];
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
static int handle_cancel_request(Client* client, const Message* message);
static int send_error_response(SSL* ssl, const char* error_message);
static int send_generic_response(Client* client, MessageType type, const char* data, int arg_count, ...);
static void client_message_loop(Client* client);
static void cleanup_client(Client* client);
static void add_client_to_list(Client* client);
static void remove_client_from_list(Client* client);
static void broadcast_status_update(void);
static bool is_user_authenticated(const char* username, const char* password); // 인증 함수 프로토타입 추가
static void load_users_from_file(const char* filename); // 사용자 정보 로드 함수 프로토타입 추가
// 전역 변수
static int server_sock;
static bool running = true;
static int self_pipe[2];
static SSLManager ssl_manager;
static ResourceManager* resource_manager = NULL;
static ReservationManager* reservation_manager = NULL;
static SessionManager* session_manager = NULL;
static HashTable* user_credentials = NULL; // 사용자 정보 해시 테이블 추가
static Client* client_list[MAX_CLIENTS];
static int num_clients = 0;
static pthread_mutex_t client_list_mutex;

#define MAX_PRIORITY 10

// 메시지 삽입 (우선순위 큐)
void enqueue_message(Client* client, Message* m) {
    int p = m->priority;
    if (p < 0) p = 0;
    if (p > MAX_PRIORITY) p = MAX_PRIORITY;
    MsgNode* node = (MsgNode*)malloc(sizeof(MsgNode));
    node->msg = m;
    node->next = NULL;
    if (!client->queues[p]) {
        client->queues[p] = client->tails[p] = node;
    } else {
        client->tails[p]->next = node;
        client->tails[p] = node;
    }
}

// 메시지 꺼내기 (우선순위 높은 것부터)
Message* dequeue_message(Client* client) {
    for (int p = MAX_PRIORITY; p >= 0; --p) {
        if (client->queues[p]) {
            MsgNode* node = client->queues[p];
            Message* m = node->msg;
            client->queues[p] = node->next;
            if (!client->queues[p]) client->tails[p] = NULL;
            free(node);
            return m;
        }
    }
    return NULL;
}

// 큐 정리 (메모리 해제)
void cleanup_client_queue(Client* client) {
    for (int p = 0; p <= MAX_PRIORITY; ++p) {
        MsgNode* node = client->queues[p];
        while (node) {
            MsgNode* next = node->next;
            message_destroy(node->msg);
            free(node);
            node = next;
        }
        client->queues[p] = client->tails[p] = NULL;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Server", "사용법: %s <포트>", argv[0]);
        return 1;
    }
    if (init_server(atoi(argv[1])) != 0) {
        utils_report_error(ERROR_NETWORK_SOCKET_CREATION_FAILED, "Main", "서버 초기화 실패");
        cleanup_server();
        return 1;
    }
    // LOG_INFO("Main", "서버가 클라이언트 연결을 기다립니다...");
    while (running) {
        struct pollfd fds[2];
        fds[0].fd = server_sock;
        fds[0].events = POLLIN;
        fds[1].fd = self_pipe[0];
        fds[1].events = POLLIN;
        int ret = poll(fds, 2, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            utils_report_error(ERROR_NETWORK_SOCKET_OPTION_FAILED, "Main", "Poll 에러: %s", strerror(errno));
            break;
        }
        if (ret == 0) {
            Device devices[MAX_DEVICES];
            int count = resource_get_device_list(resource_manager, devices, MAX_DEVICES);
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
            
            // network_accept_client 함수를 사용하여 클라이언트 연결 처리
            client->ssl_handler = network_accept_client(server_sock, &ssl_manager, client->ip);
            if (!client->ssl_handler) {
                free(client);
                continue;
            }
            
            client->ssl = client->ssl_handler->ssl;
            client->socket_fd = client->ssl_handler->socket_fd;
            client->last_activity = time(NULL);
            
            pthread_t thread;
            if (pthread_create(&thread, NULL, client_thread_func, client) != 0) {
                utils_report_error(ERROR_SESSION_CREATION_FAILED, "Main", "클라이언트 스레드 생성 실패");
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
    int count = resource_get_device_list(resource_manager, devices, MAX_DEVICES);
    if (count < 0) return;
    Message* response = message_create(MSG_STATUS_UPDATE, NULL);
    if (response) {
        if (!message_fill_status_response_args(response, devices, count, resource_manager, reservation_manager)) {
            message_destroy(response);
            return;
        }
        pthread_mutex_lock(&client_list_mutex);
        for (int i = 0; i < num_clients; i++) {
            if (client_list[i] && client_list[i]->state == SESSION_LOGGED_IN) {
                network_send_message(client_list[i]->ssl, response);
            }
        }
        pthread_mutex_unlock(&client_list_mutex);
        message_destroy(response);
    }
}

static void client_message_loop(Client* client) {
    while (running) {
        // 1. 메시지 수신 후 큐에 삽입
        Message* msg = message_receive(client->ssl);
        if (msg) {
            enqueue_message(client, msg);
            client->last_activity = time(NULL);
        } else {
            // LOG_INFO("Thread", "클라이언트(%s)가 연결을 종료했습니다.", client->ip);
            break;
        }

        // 2. 큐에 있는 모든 메시지를 우선순위 순서대로 처리
        Message* to_process;
        while ((to_process = dequeue_message(client)) != NULL) {
            handle_client_message(client, to_process);
            message_destroy(to_process);
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
    cleanup_client_queue(client); // 메시지 큐 메모리 해제
    if (client->state == SESSION_LOGGED_IN) close_session(session_manager, client->username);
    if (client->ssl_handler) network_cleanup_ssl_handler(client->ssl_handler);
    if (client->socket_fd >= 0) close(client->socket_fd);
    free(client);
}

static void* client_thread_func(void* arg) {
    Client* client = (Client*)arg;
    // SSL 핸들러와 핸드셰이크는 이미 main에서 network_accept_client()로 처리됨
    add_client_to_list(client);
    client_message_loop(client);
    remove_client_from_list(client);
    cleanup_client(client);
    return NULL;
}

static int process_device_reservation(Client* client, const char* device_id, int duration_sec) {
    if (!resource_is_device_available(resource_manager, device_id)) {
        char error_msg[256];
        Reservation* active_res = reservation_get_active_for_device(reservation_manager, resource_manager, device_id);
        if (active_res) {
            snprintf(error_msg, sizeof(error_msg), "사용 불가: '%s'님이 사용 중입니다.", active_res->username);
        } else {
            strncpy(error_msg, "현재 사용 불가 또는 점검 중인 장비입니다.", sizeof(error_msg)-1);
        }
       return send_error_response(client->ssl, error_msg);
    }
    time_t start = time(NULL);
    time_t end = start + duration_sec;
    uint32_t new_res_id = reservation_create(reservation_manager, device_id, client->username, start, end, "User Reservation");
    if (new_res_id == 0) {
        return send_error_response(client->ssl, "예약 생성에 실패했습니다 (시간 중복 등).");
    }
    if (!resource_update_device_status(resource_manager, device_id, DEVICE_RESERVED, new_res_id)) {
        reservation_cancel(reservation_manager, new_res_id, "system");
        return send_error_response(client->ssl, "서버 내부 오류: 예약 상태 동기화 실패");
    }
    broadcast_status_update();
    Message* response = message_create(MSG_RESERVE_RESPONSE, "success");
    if (response) {
        Device updated_device;
        Device* devices_ptr = (Device*)utils_hashtable_get(resource_manager->devices, device_id);
        if (devices_ptr) {
            memcpy(&updated_device, devices_ptr, sizeof(Device));
            response->args[0] = strdup(device_id);
            response->arg_count = 1;
            message_fill_status_response_args(response, &updated_device, 1, resource_manager, reservation_manager);
        }
        network_send_message(client->ssl, response);
        message_destroy(response);
    }
    return 0;
}
static bool is_user_authenticated(const char* username, const char* password) {
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
static int handle_login_request(Client* client, const Message* message) {
    if (message->arg_count < 2) {
        LOG_WARNING("Auth", "로그인 요청 실패: 인수 부족 (필요: 2, 받음: %d)", message->arg_count);
        return send_error_response(client->ssl, "로그인 정보가 부족합니다.");
    }
    const char* user = message->args[0];
    const char* pass = message->args[1];

    LOG_INFO("Auth", "로그인 시도: 사용자='%s', IP=%s", user, client->ip);

    // 1단계: 사용자 자격 증명 확인
    if (!is_user_authenticated(user, pass)) {
        LOG_WARNING("Auth", "로그인 실패: 사용자 '%s'의 자격 증명이 올바르지 않습니다. (IP: %s)", user, client->ip);
        return send_error_response(client->ssl, "아이디 또는 비밀번호가 틀립니다.");
    }

    LOG_INFO("Auth", "사용자 자격 증명 확인 성공: '%s'", user);

    // 2단계: 세션 생성 (중복 로그인 방지)
    // [버그 수정] 기존 코드에서는 create_session이 두 번 호출되었습니다.
    // 이제 인증 후 한 번만 호출하여 반환값으로 중복 여부를 확인합니다.
    ServerSession* session = create_session(session_manager, user, client->ip, 0);
    if (!session) {
        // create_session은 사용자가 이미 존재할 경우 NULL을 반환합니다.
        LOG_WARNING("Auth", "로그인 실패: 사용자 '%s'는 이미 로그인되어 있습니다. (IP: %s)", user, client->ip);
        return send_error_response(client->ssl, "이미 로그인된 사용자입니다.");
    }

    // 3단계: 로그인 성공 처리
    client->state = SESSION_LOGGED_IN;
    strncpy(client->username, user, MAX_USERNAME_LENGTH - 1);
    client->username[MAX_USERNAME_LENGTH - 1] = '\0';
    
    LOG_INFO("Auth", "사용자 '%s'가 IP주소 %s 에서 로그인했습니다.", user, client->ip);

    // 4단계: 클라이언트에 성공 응답 전송
    send_generic_response(client, MSG_LOGIN, "success", 1, user);

    // 5단계: 로그인 직후, 클라이언트가 다음 화면을 그릴 수 있도록 장비 현황 전송
    handle_status_request(client, NULL); 

    return 0;
}
static int handle_status_request(Client* client, const Message* message) {
    (void)message; 
    Device devices[MAX_DEVICES];
    int count = resource_get_device_list(resource_manager, devices, MAX_DEVICES);
    if (count < 0) return send_error_response(client->ssl, "서버에서 장비 목록을 가져오는 데 실패했습니다.");
    Message* response = message_create_status_response(devices, count, resource_manager, reservation_manager);
    if (response) {
        network_send_message(client->ssl, response);
        message_destroy(response);
    }
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
static int handle_cancel_request(Client* client, const Message* message) {
    if (message->arg_count < 1) {
        return send_error_response(client->ssl, "예약 취소 정보(장비 ID)가 부족합니다.");
    }

    const char* device_id = message->args[0];
    
    // 예약 정보 확인
    Reservation* res = reservation_get_active_for_device(reservation_manager, resource_manager, device_id);

    // 본인의 예약이 맞는지 확인
    if (!res || strcmp(res->username, client->username) != 0) {
        return send_error_response(client->ssl, "취소할 수 있는 예약이 아니거나 권한이 없습니다.");
    }

    // 예약 취소 로직 호출
    if (reservation_cancel(reservation_manager, res->id, client->username)) {
        // 장비 상태를 'available'로 변경
        resource_update_device_status(resource_manager, device_id, DEVICE_AVAILABLE, 0);
        
        // 모든 클라이언트에 상태 변경 전파
        broadcast_status_update();

        // 요청한 클라이언트에 성공 응답 전송
        send_generic_response(client, MSG_CANCEL_RESPONSE, "success", 0);
    } else {
        send_error_response(client->ssl, "알 수 없는 오류로 예약 취소에 실패했습니다.");
    }

    return 0;
}
static int handle_client_message(Client* client, const Message* message) {
    if (!client || !message) return -1;
    // 로그인되지 않은 상태에서는 로그인 요청만 허용
    if (client->state != SESSION_LOGGED_IN && message->type != MSG_LOGIN) {
        return send_error_response(client->ssl, "로그인이 필요한 서비스입니다.");
    }
    switch (message->type) {
        case MSG_LOGIN: 
            return handle_login_request(client, message); // 리팩토링된 함수 호출
        case MSG_STATUS_REQUEST: 
            return handle_status_request(client, message);
        case MSG_RESERVE_REQUEST: 
            return handle_reserve_request(client, message);
        case MSG_CANCEL_REQUEST: 
            return handle_cancel_request(client, message);
        default: 
            return send_error_response(client->ssl, "알 수 없거나 처리할 수 없는 요청입니다.");
    }
}

static void signal_handler(int signum) {
    (void)signum;
    if (pipe(self_pipe) == -1) { 
        utils_report_error(ERROR_FILE_OPERATION_FAILED, "Server", "pipe 생성 실패"); 
        return; 
    }
    (void)write(self_pipe[1], "s", 1);
}

static int init_server(int port) {
    if (pipe(self_pipe) == -1) { 
        utils_report_error(ERROR_FILE_OPERATION_FAILED, "Server", "pipe 생성 실패"); 
        return -1; 
    }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    if (utils_init_logger("logs/server.log") < 0) return -1;
    if (network_init_ssl_manager(&ssl_manager, true, "certs/server.crt", "certs/server.key") < 0) return -1;
    if (init_ui() < 0) return -1;
    
    resource_manager = resource_init_manager();
    reservation_manager = reservation_init_manager(resource_manager, broadcast_status_update);
    session_manager = init_session_manager();
    load_users_from_file("users.txt"); // 사용자 정보 로드 추가
    
    if (!resource_manager || !reservation_manager || !session_manager || !user_credentials) return -1;
    
    server_sock = network_init_server_socket(port);
    if (server_sock < 0) return -1;
    
    return 0;
}

static void cleanup_server(void) {
    running = false;
    
    if (user_credentials) {
        utils_hashtable_destroy(user_credentials);
        user_credentials = NULL;
        LOG_INFO("Auth", "사용자 정보 해시 테이블 정리 완료");
    }
    
    if (session_manager) {
        cleanup_session_manager(session_manager);
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
    
    cleanup_ui();
    network_cleanup_ssl_manager(&ssl_manager);
    utils_cleanup_logger();
    
    if (server_sock >= 0) {
        close(server_sock);
        server_sock = -1;
    }
    
    close(self_pipe[0]);
    close(self_pipe[1]);
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
static int send_generic_response(Client* client, MessageType type, const char* data, int arg_count, ...) {
    if (!client || !client->ssl) return -1;

    Message* response = message_create(type, data);
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

/**
 * @brief 클라이언트에 에러 메시지를 전송합니다. (기존 함수 활용)
 * @param ssl 클라이언트의 SSL 객체
 * @param error_message 보낼 에러 메시지
 * @return 성공 시 0, 실패 시 -1
 */
static int send_error_response(SSL* ssl, const char* error_message) {
    if (!ssl || !error_message) return -1;
    Message* response = message_create_error(error_message);
    if (!response) return -1;
    int ret = network_send_message(ssl, response);
    message_destroy(response);
    return ret;
}

static void load_users_from_file(const char* filename) {
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
