#include "../include/logger.h"
#include "../include/message.h"
#include "../include/network.h"
#include "../include/session.h"
#include "../include/ui.h"
#include "../include/performance.h"
#include "../include/resource.h"
#include "../include/reservation.h"

// 파일 상단에 추가할 함수 선언
static void signal_handler(int signum);
static int connect_to_server(const char* server_ip, int port);
static void handle_server_message(const Message* message);
static void handle_device_reservation(int device_index);
static DeviceStatus string_to_device_status(const char* status_str);
static void handle_main_menu(void); // [이름 변경]
static void cleanup_resources(void);
// 파일 상단에 추가할 extern 선언
extern UIManager* global_ui_manager;

// 상태 상수 정의
#define STATE_MAIN_MENU 0

/* 전역 변수 */
static ClientSession client_session;
static SSLManager ssl_manager;
static PerformanceStats* perf_stats = NULL;
static bool running = true;
static pthread_mutex_t shutdown_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool shutdown_requested = false;
static pthread_cond_t shutdown_cond = PTHREAD_COND_INITIALIZER;
static Device* device_list = NULL;
static int device_count = 0;
ITEM** device_menu_items = NULL;
extern UIManager* global_ui_manager;

/* 시그널 핸들러 */
static void signal_handler(int signum) {
    LOG_INFO("Client", "시그널 수신: %d", signum);
    running = false;

    pthread_mutex_lock(&shutdown_state_mutex);
    shutdown_requested = true;
    pthread_cond_signal(&shutdown_cond);
    pthread_mutex_unlock(&shutdown_state_mutex);

    LOG_INFO("Client", "자원 정리 시작");
    cleanup_client_session(&client_session);
    cleanup_performance_stats(perf_stats);
    cleanup_ssl_manager(&ssl_manager);
    cleanup_ui();
    cleanup_logger();
    LOG_INFO("Client", "프로그램 종료");
    exit(0);
}

/* 서버 연결 함수 */
static int connect_to_server(const char* server_ip, int port) {
    LOG_INFO("Client", "서버 연결 시도: %s:%d", server_ip, port);
    
    int sock_fd = init_client_socket(server_ip, port);
    if (sock_fd < 0) {
        LOG_ERROR("Client", "서버 연결 실패");
        return -1;
    }

    SSLHandler* ssl_handler = create_ssl_handler(&ssl_manager, sock_fd);
    if (!ssl_handler) {
        LOG_ERROR("Client", "SSL 핸들러 생성 실패");
        close(sock_fd);
        return -1;
    }

    LOG_INFO("Client", "SSL 핸드셰이크 시도");
    int ret = handle_ssl_handshake(ssl_handler);
    if (ret != 0) {
        LOG_ERROR("Client", "SSL 핸드셰이크 실패");
        cleanup_ssl_handler(ssl_handler);
        close(sock_fd);
        return -1;
    }

    client_session.ssl = ssl_handler->ssl;
    client_session.state = SESSION_CONNECTING;
    strncpy(client_session.server_ip, server_ip, sizeof(client_session.server_ip) - 1);
    client_session.server_port = port;

    // 핸드셰이크에 성공했으므로 ssl_handler 자체는 해제하지 않음
    // ssl_handler->ssl의 소유권은 client_session으로 이전되었으며,
    // 프로그램 종료 시 cleanup_client_session에서 SSL_free()를 통해 해제됨.
    // 핸들러 컨테이너는 핸드셰이크 실패 시에만 cleanup_ssl_handler를 통해 정리됨.

    LOG_INFO("Client", "서버 연결 성공");
    return 0;
}
static void cleanup_resources(void) {
    LOG_INFO("Client", "자원 정리 시작");

    // 1. 동적으로 할당된 장비 목록 메모리 해제
    if (device_list) {
        free(device_list);
        device_list = NULL;
    }

    // 2. 클라이언트 세션 및 SSL 연결 정리
    cleanup_client_session(&client_session);
    // 3. SSL 관리자(컨텍스트 등) 정리
    cleanup_ssl_manager(&ssl_manager);
    // 4. ncurses UI 종료
    cleanup_ui();
    // 5. 로거 종료
    cleanup_logger();

    LOG_INFO("Client", "클라이언트 프로그램이 정상적으로 종료되었습니다.");
}
/* 서버 메시지 처리 함수 */
static void handle_server_message(const Message* message) {
    switch (message->type) {
        case MSG_STATUS_RESPONSE: {
            if (device_list) {
                free(device_list);
                device_list = NULL;
            }
            
            device_count = message->arg_count / 4;
            device_list = (Device*)malloc(sizeof(Device) * device_count);
            if (!device_list) {
                show_error_message("장비 목록 메모리 할당 실패");
                return;
            }

            const char** menu_items = (const char**)malloc(sizeof(char*) * device_count);
            if (!menu_items) {
                show_error_message("메뉴 아이템 메모리 할당 실패");
                free(device_list);
                device_list = NULL;
                return;
            }

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

            // [호출 변경] create_menu 함수 호출
             int choice = create_menu(global_ui_manager, "장비 목록 (선택: Enter, 뒤로가기: ESC)", menu_items, device_count);
            // ... (이전과 동일)

            for (int i = 0; i < device_count; i++) {
                free((void*)menu_items[i]);
            }
            free(menu_items);

            if (choice >= 0) {
                handle_device_reservation(choice);
            }
            break;
        }
        // ... (다른 case 문들은 기존과 동일) ...
    }
}

/* [이름 변경] 메인 메뉴를 표시하고 사용자 입력을 처리하는 함수 */
static void handle_main_menu(void) {
    const char* items[] = {
        "장비 현황 조회",
        "종료"
    };
    // [호출 변경] create_menu 함수 호출
    int choice = create_menu(global_ui_manager, "메인 메뉴", items, 2);

    switch (choice) {
        case 0: {
            Message* msg = create_message(MSG_STATUS_REQUEST, NULL);
            if (msg) {
                send_message(client_session.ssl, msg);
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

/* 장비 예약 처리 */
static void handle_device_reservation(int device_index) {
    if (!device_list || device_index < 0 || device_index >= device_count) {
        show_error_message("잘못된 장비 선택");
        return;
    }

    if (device_list[device_index].status != DEVICE_AVAILABLE) {
        show_error_message("예약할 수 없는 장비입니다.");
        return;
    }

    // 예약 요청 메시지 생성 및 전송
    Message* msg = create_reservation_message(device_list[device_index].id);
    if (!msg) {
        show_error_message("예약 요청 생성 실패");
        return;
    }

    if (send_message(client_session.ssl, msg) < 0) {
        show_error_message("예약 요청 전송 실패");
    }

    cleanup_message(msg);
    free(msg);
}

/* 메인 함수 */
int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "사용법: %s <서버 IP> <포트>\n", argv[0]);
        return 1;
    }

    // 로거 초기화
 signal(SIGINT, signal_handler);

    // --- 초기화 ---
    if (init_logger("logs/client.log") < 0 || init_ui() < 0 || init_ssl_manager(&ssl_manager, false, NULL, NULL) < 0) {
        fprintf(stderr, "초기화 실패\n");
        cleanup_resources(); // 초기화 실패 시에도 정리 함수 호출
        return 1;
    }
    
    if (connect_to_server(argv[1], atoi(argv[2])) < 0) {
        cleanup_resources(); // 연결 실패 시 정리 함수 호출
        return 1;
    }

    // --- 메인 루프 ---
    while (running) {
        handle_main_menu();
        if (!running) break;

        bool response_handled = false;
        while (!response_handled && running) {
struct pollfd fds[1];
            fds[0].fd = client_session.socket_fd;
            fds[0].events = POLLIN;

            int ret = poll(fds, 1, 5000); 

            if (ret > 0 && (fds[0].revents & POLLIN)) {
                Message received_msg;
                if (receive_message(client_session.ssl, &received_msg) == 0) {
                    handle_server_message(&received_msg);
                    cleanup_message(&received_msg);
                    response_handled = true;
                } else {
                    LOG_ERROR("Client", "서버 연결이 끊겼습니다.");
                    running = false;
                }
            } else if (ret == 0) {
                show_error_message("서버 응답이 없습니다.");
                break;
            } else {
                if (errno == EINTR) continue;
                LOG_ERROR("Client", "Poll 에러: %s", strerror(errno));
                running = false;
            }
        }
    }

    // --- 정리 ---
    cleanup_resources();
    return 0;
}

// 문자열을 DeviceStatus enum으로 변환하는 유틸리티 함수
static DeviceStatus string_to_device_status(const char* status_str) {
    if (strcmp(status_str, "reserved") == 0) return DEVICE_RESERVED;
    if (strcmp(status_str, "maintenance") == 0) return DEVICE_MAINTENANCE;
    return DEVICE_AVAILABLE;
}

static Message* create_reservation_message(const char* device_id) {
    return create_message(MSG_RESERVE_REQUEST, device_id);
}