#include "../include/logger.h"
#include "../include/message.h"
#include "../include/network.h"
#include "../include/session.h"
#include "../include/ui.h"
#include "../include/resource.h"
#include "../include/reservation.h"

/* --- 함수 프로토타입 --- */
static void signal_handler(int signum);
static void cleanup_resources(void);
static int connect_to_server(const char* server_ip, int port);
static void handle_server_message(const Message* message);
static void handle_main_menu(void);
static void handle_device_reservation(int device_index);
static DeviceStatus string_to_device_status(const char* status_str);
static void handle_logged_in_menu(void);
static bool handle_login(void);

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
// 로그인 상태에 따라 적절한 메뉴만 호출
    if (client_session.state == SESSION_LOGGED_IN) {
        handle_logged_in_menu();
    } else {
        handle_main_menu();
    }

    if (!running) break;

        // 서버로부터 메시지를 기다리는 poll 로직 (UI 상호작용 후 즉시 실행)
        struct pollfd fds[2];
        fds[0].fd = client_session.socket_fd;
        fds[0].events = POLLIN;
        fds[1].fd = self_pipe[0];
        fds[1].events = POLLIN;

        // 0ms 타임아웃으로 즉시 확인
        int ret = poll(fds, 2, 0); 

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (fds[1].revents & POLLIN) {
            running = false;
            continue;
        }
        if (fds[0].revents & POLLIN) {
            // [수정] Message 포인터로 반환값을 받도록 변경
            Message* msg = receive_message(client_session.ssl); 
            
            // [수정] 포인터가 NULL이 아닌지 확인하여 성공 여부 판단
            if (msg) { 
                handle_server_message(msg);
                cleanup_message(msg); // 메시지 내부의 동적 할당된 args 해제
                free(msg);            // 메시지 구조체 자체의 메모리 해제
            } else {
                // 수신 실패 또는 연결 종료로 간주
                running = false;
            }
        }
    }

    cleanup_resources();
    return 0;
}

// 로그인 이전의 메인 메뉴
static void handle_main_menu(void) {
    const char* items[] = { "로그인", "종료" };
    int choice = create_menu(global_ui_manager, "메인 메뉴", items, 2);

    switch (choice) {
        case 0: // 로그인
            handle_login();
            break;
        case 1: // 종료
        case -1: // ESC
            running = false;
            break;
    }
}

// 로그인 이후의 메인 메뉴 (새로 추가)
static void handle_logged_in_menu(void) {
    const char* items[] = { "장비 현황 조회 및 예약", "로그아웃" };
    int choice = create_menu(global_ui_manager, "메인 메뉴", items, 2);

    switch (choice) {
        case 0: { // 장비 현황 조회
            Message* msg = create_message(MSG_STATUS_REQUEST, NULL);
            if (msg) {
                if(send_message(client_session.ssl, msg) < 0) running = false;
                cleanup_message(msg);
                free(msg);
            }
            break;
        }
        case 1: // 로그아웃
            // 세션 상태를 변경하고, 다음 루프에서 로그인 메뉴를 표시하도록 함
            client_session.state = SESSION_DISCONNECTED;
            show_success_message("로그아웃되었습니다.");
            sleep(1);
            break;
        case -1: // ESC
             // 현재는 아무 동작 안 함. 필요 시 종료 로직 추가 가능
            break;
    }
}

static void handle_server_message(const Message* message) {
       switch (message->type) {
        // [추가] 로그인 응답 처리
        case MSG_LOGIN:
            if (strcmp(message->data, "success") == 0) {
                client_session.state = SESSION_LOGGED_IN;
                show_success_message("로그인 성공!");
                sleep(1); // 1초간 메시지 표시
            }
            // 에러의 경우 MSG_ERROR로 오므로 별도 처리 필요 없음
            break;
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

// 사용자 로그인을 처리하는 함수 (실제 구현 시에는 사용자 입력 필요)
static bool handle_login() {
    // UI에 사용자 이름과 비밀번호 입력 필드를 표시하고 값을 받아와야 함.
    // 여기서는 하드코딩된 값으로 대체.
    const char* username = "test";
    const char* password = "1234";

    show_success_message("로그인 시도 중...");

    // 로그인 메시지 생성
    Message* msg = create_message(MSG_LOGIN, NULL);
    if (!msg) {
        show_error_message("메시지 생성 실패");
        return false;
    }

    // 인자에 아이디와 비밀번호 추가
    msg->args[0] = strdup(username);
    msg->args[1] = strdup(password);
    msg->arg_count = 2;

    // 메시지 전송
    if (send_message(client_session.ssl, msg) < 0) {
        running = false;
        cleanup_message(msg);
        free(msg);
        return false;
    }

    cleanup_message(msg);
    free(msg);
    
    // 이 함수는 메시지 전송까지만 담당. 결과는 handle_server_message 에서 처리.
    return true; 
}