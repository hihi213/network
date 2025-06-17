#include "../include/logger.h"
#include "../include/message.h"
#include "../include/network.h"
#include "../include/session.h"
#include "../include/ui.h"
#include "../include/resource.h"
#include "../include/reservation.h"
#include <unistd.h> // self-pipe 및 close 함수를 위해 추가
#include <signal.h> // signal 함수를 위해 추가

/* --- 함수 프로토타입 --- */
static void signal_handler(int signum);
static void cleanup_resources(void);
static int connect_to_server(const char* server_ip, int port);
static void handle_server_message(const Message* message);
static void handle_main_menu(void);
static void handle_device_reservation(int device_index);
static DeviceStatus string_to_device_status(const char* status_str);

/* --- 전역 변수 --- */
extern UIManager* global_ui_manager;
static ClientSession client_session;
static SSLManager ssl_manager;
static bool running = true;
static int self_pipe[2];

static Device* device_list = NULL;
static int device_count = 0;


/* --- 함수 정의 --- */

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "사용법: %s <서버 IP> <포트>\n", argv[0]);
        return 1;
    }

    if (init_logger("logs/client.log") < 0) return 1;
    if (pipe(self_pipe) == -1) { perror("pipe"); return 1; }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (init_ui() < 0 || init_ssl_manager(&ssl_manager, false, NULL, NULL) < 0) {
        cleanup_resources();
        return 1;
    }
    
    if (connect_to_server(argv[1], atoi(argv[2])) < 0) {
        cleanup_resources();
        return 1;
    }

    while (running) {
        handle_main_menu();
        if (!running) break;

        struct pollfd fds[2];
        fds[0].fd = client_session.socket_fd;
        fds[0].events = POLLIN;
        fds[1].fd = self_pipe[0];
        fds[1].events = POLLIN;

        int ret = poll(fds, 2, -1);

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (fds[1].revents & POLLIN) {
            running = false;
            continue;
        }
        if (fds[0].revents & POLLIN) {
            Message msg;
            if (receive_message(client_session.ssl, &msg) > 0) {
                handle_server_message(&msg);
                cleanup_message(&msg);
            } else {
                running = false;
            }
        }
    }

    cleanup_resources();
    return 0;
}

static void handle_main_menu(void) {
    const char* items[] = { "장비 현황 조회", "종료" };
    int choice = create_menu(global_ui_manager, "메인 메뉴", items, 2);

    switch (choice) {
        case 0: {
            Message* msg = create_message(MSG_STATUS_REQUEST, NULL);
            if (msg) {
                if(send_message(client_session.ssl, msg) < 0) running = false;
                cleanup_message(msg);
            }
            break;
        }
        case 1:
        case -1:
            running = false;
            break;
    }
}

static void handle_server_message(const Message* message) {
    switch (message->type) {
        case MSG_STATUS_RESPONSE: {
            if (device_list) free(device_list);
            
            device_count = message->arg_count / 4;
            device_list = (Device*)malloc(sizeof(Device) * device_count);
            const char** menu_items = (const char**)malloc(sizeof(char*) * device_count);
            if (!device_list || !menu_items) { /* 메모리 할당 실패 처리 */ return; }

            for (int i = 0; i < device_count; i++) {
                int base_idx = i * 4;
                strncpy(device_list[i].id, message->args[base_idx], MAX_ID_LENGTH - 1);
                strncpy(device_list[i].name, message->args[base_idx + 1], MAX_DEVICE_NAME_LENGTH - 1);
                strncpy(device_list[i].type, message->args[base_idx + 2], MAX_DEVICE_TYPE_LENGTH - 1);
                device_list[i].status = string_to_device_status(message->args[base_idx + 3]);

                char* item_str = (char*)malloc(256);
                snprintf(item_str, 256, "%-10s | %-25s | %-15s | %s",
                         device_list[i].id, device_list[i].name, device_list[i].type, message->args[base_idx+3]);
                menu_items[i] = item_str;
            }

            int choice = create_menu(global_ui_manager, "장비 목록 (Enter: 예약, ESC: 뒤로)", menu_items, device_count);

            for (int i = 0; i < device_count; i++) free((void*)menu_items[i]);
            free(menu_items);

            if (choice >= 0) handle_device_reservation(choice);
            break;
        }
        case MSG_RESERVE_RESPONSE:
            show_success_message(message->data);
            getch();
            break;
        case MSG_ERROR:
            show_error_message(message->data);
            getch();
            break;
        default:
            LOG_ERROR("Message", "알 수 없는 메시지 타입: %d", message->type);
            break;
    }
}
/**
 * @brief 시그널(Ctrl+C)을 안전하게 처리하는 핸들러 (Self-Pipe 트릭 사용)
 */
static void signal_handler(int signum) {
    (void)signum;
    // 핸들러 내에서는 async-signal-safe 함수인 write만 호출
    (void)write(self_pipe[1], "s", 1);
}

/**
 * @brief 프로그램 종료 시 모든 자원을 정리하는 함수
 */
static void cleanup_resources(void) {
    LOG_INFO("Client", "자원 정리 시작");
    if (device_list) {
        free(device_list);
        device_list = NULL;
    }
    cleanup_client_session(&client_session);
    cleanup_ssl_manager(&ssl_manager);
    cleanup_ui();
    cleanup_logger();

    // self-pipe 파일 디스크립터 닫기
    close(self_pipe[0]);
    close(self_pipe[1]);
    LOG_INFO("Client", "클라이언트 프로그램이 정상적으로 종료되었습니다.");
}

/**
 * @brief 서버 연결 및 SSL 핸드셰이크 수행
 */
static int connect_to_server(const char* server_ip, int port) {
    LOG_INFO("Client", "서버 연결 시도: %s:%d", server_ip, port);
    int sock_fd = init_client_socket(server_ip, port);
    if (sock_fd < 0) {
        LOG_ERROR("Client", "서버 연결 실패");
        return -1;
    }

    SSLHandler* ssl_handler = create_ssl_handler(&ssl_manager, sock_fd);
    if (!ssl_handler || handle_ssl_handshake(ssl_handler) != 0) {
        LOG_ERROR("Client", "SSL 설정 또는 핸드셰이크 실패");
        if(ssl_handler) cleanup_ssl_handler(ssl_handler);
        close(sock_fd);
        return -1;
    }

    client_session.ssl = ssl_handler->ssl;
    client_session.socket_fd = sock_fd;
    strncpy(client_session.server_ip, server_ip, sizeof(client_session.server_ip) - 1);
    client_session.server_port = port;
    client_session.state = SESSION_CONNECTING;
    LOG_INFO("Client", "서버 연결 성공");
    return 0;
}




/**
 * @brief 사용자가 선택한 장비에 대한 예약 요청 전송
 */
static void handle_device_reservation(int device_index) {
    if (!device_list || device_index < 0 || device_index >= device_count) {
        show_error_message("잘못된 장비 선택입니다.");
        getch();
        return;
    }
    if (device_list[device_index].status != DEVICE_AVAILABLE) {
        show_error_message("예약할 수 없는 장비입니다.");
        getch();
        return;
    }

    Message* msg = create_reservation_message(device_list[device_index].id);
    if (!msg) {
        show_error_message("예약 요청 생성 실패");
        getch();
        return;
    }
    if (send_message(client_session.ssl, msg) < 0) {
        show_error_message("예약 요청 전송 실패");
        getch();
    }
    cleanup_message(msg);
}

/**
 * @brief 문자열을 DeviceStatus enum으로 변환하는 유틸리티 함수
 */
static DeviceStatus string_to_device_status(const char* status_str) {
    if (strcmp(status_str, "reserved") == 0) return DEVICE_RESERVED;
    if (strcmp(status_str, "maintenance") == 0) return DEVICE_MAINTENANCE;
    return DEVICE_AVAILABLE;
}

