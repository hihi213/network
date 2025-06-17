#include "../include/logger.h"
#include "../include/message.h"
#include "../include/network.h"
#include "../include/session.h"
#include "../include/ui.h"
#include "../include/performance.h"
#include "../include/resource.h"
#include "../include/reservation.h"

// 파일 상단에 추가할 함수 선언
static void create_device_menu(void);
static void cleanup_device_menu_items(void);
static WINDOW* create_menu_window(UIManager* manager, int height);
static void handle_device_reservation(int device_index);
static DeviceStatus string_to_device_status(const char* status_str);
static void create_menu(UIManager* manager, const char** items, int count);
static void set_status_message(UIManager* manager, const char* message);
static Message* create_reservation_message(const char* device_id);

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
static int current_state = STATE_MAIN_MENU;
static Device* device_list = NULL;
static int device_count = 0;
ITEM** device_menu_items = NULL;

static void show_main_menu(void) {
    const char* menu_items[] = {
        "1. 장비 현황 조회",
        "2. 종료",
        NULL
    };
    create_menu(global_ui_manager, menu_items, 2);
    set_status_message(global_ui_manager, "메뉴를 선택하세요 (1-2)");
    current_state = STATE_MAIN_MENU;
}

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

/* 서버 메시지 처리 함수 */
static int handle_server_message(const Message* message) {
    switch (message->type) {
        case MSG_STATUS_RESPONSE: {
            // 기존 장비 목록 해제
            if (device_list) {
                free(device_list);
                device_list = NULL;
            }
            
            device_count = message->arg_count / 4;
            device_list = (Device*)malloc(sizeof(Device) * device_count);
            
            if (!device_list) {
                show_error_message("장비 목록 메모리 할당 실패");
                return -1;
            }
            
            // 장비 정보 파싱
            for (int i = 0; i < device_count; i++) {
                int base_idx = i * 4;
                strncpy(device_list[i].id, message->args[base_idx], MAX_DEVICE_ID_LEN - 1);
                strncpy(device_list[i].name, message->args[base_idx + 1], MAX_DEVICE_NAME_LENGTH - 1);
                strncpy(device_list[i].type, message->args[base_idx + 2], MAX_DEVICE_TYPE_LENGTH - 1);
                device_list[i].status = string_to_device_status(message->args[base_idx + 3]);
            }
            
            // 장비 목록 메뉴 생성
            create_device_menu();
            break;
        }
        case MSG_ERROR:
            show_error_message(message->data);
            break;
        default:
            LOG_ERROR("Message", "알 수 없는 메시지 타입: %d", message->type);
            break;
    }
    return 0;
}

/* 장비 목록 메뉴 생성 */
static void create_device_menu(void) {
    cleanup_device_menu_items();
    device_menu_items = (ITEM**)malloc(sizeof(ITEM*) * (device_count + 1));
    if (!device_menu_items) {
        LOG_ERROR("Client", "메뉴 아이템 메모리 할당 실패");
        return;
    }

    for (int i = 0; i < device_count; i++) {
        char menu_text[256];
        snprintf(menu_text, sizeof(menu_text), "%-10s | %-25s | %-15s | %s",
                device_list[i].id, device_list[i].name, device_list[i].type, get_device_status_string(device_list[i].status));
        device_menu_items[i] = new_item(menu_text, "");
        if (!device_menu_items[i]) {
            LOG_ERROR("Client", "메뉴 아이템 생성 실패");
            cleanup_device_menu_items();
            return;
        }
    }
    device_menu_items[device_count] = NULL;

    ITEM** menu_items = device_menu_items;
    MENU* menu = new_menu(menu_items);
    if (!menu) {
        LOG_ERROR("Client", "메뉴 생성 실패");
        cleanup_device_menu_items();
        return;
    }

    WINDOW* menu_win = create_menu_window(global_ui_manager, device_count + 2);
    if (!menu_win) {
        LOG_ERROR("Client", "메뉴 윈도우 생성 실패");
        free_menu(menu);
        cleanup_device_menu_items();
        return;
    }

    set_menu_win(menu, menu_win);
    set_menu_sub(menu, derwin(menu_win, device_count, 80, 1, 1));
    post_menu(menu);
    wrefresh(menu_win);

    int ch;
    while ((ch = wgetch(menu_win)) != KEY_F(1)) {
        switch (ch) {
            case KEY_DOWN:
                menu_driver(menu, REQ_DOWN_ITEM);
                break;
            case KEY_UP:
                menu_driver(menu, REQ_UP_ITEM);
                break;
            case 10: // Enter
                {
                    ITEM* cur_item = current_item(menu);
                    for (int i = 0; i < device_count; i++) {
                        if (device_menu_items[i] == cur_item) {
                            handle_device_reservation(i);
                            break;
                        }
                    }
                }
                break;
            case 27: // ESC
                unpost_menu(menu);
                free_menu(menu);
                delwin(menu_win);
                cleanup_device_menu_items();
                return;
        }
        wrefresh(menu_win);
    }

    unpost_menu(menu);
    free_menu(menu);
    delwin(menu_win);
    cleanup_device_menu_items();
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

    const char* server_ip = argv[1];
    int port = atoi(argv[2]);

    // 시그널 핸들러 설정
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 로거 초기화
    if (init_logger("logs/client.log") < 0) {
        fprintf(stderr, "로거 초기화 실패\n");
        return 1;
    }

    // UI 초기화
    if (init_ui() < 0) {
        LOG_ERROR("UI", "UI 초기화 실패");
        cleanup_logger();
        return 1;
    }

    // getch()가 100ms 동안만 입력을 기다리도록 타임아웃 설정
    timeout(100); 

    // SSL 관리자 초기화
    if (init_ssl_manager(&ssl_manager, false, NULL, NULL) < 0) {
        LOG_ERROR("SSL", "SSL 관리자 초기화 실패");
        cleanup_ui();
        cleanup_logger();
        return 1;
    }

    // 성능 통계 초기화
    perf_stats = init_performance_stats();
    if (!perf_stats) {
        LOG_ERROR("Performance", "성능 통계 초기화 실패");
        cleanup_ssl_manager(&ssl_manager);
        cleanup_ui();
        cleanup_logger();
        return 1;
    }

    // 서버 연결
    if (connect_to_server(server_ip, port) < 0) {
        LOG_ERROR("Network", "서버 연결 실패");
        cleanup_performance_stats(perf_stats);
        cleanup_ssl_manager(&ssl_manager);
        cleanup_ui();
        cleanup_logger();
        return 1;
    }

    // 메인 메뉴 표시
    show_main_menu();

    // --- 기존 pollfd 설정 부분을 아래와 같이 수정 ---
    struct pollfd fds[1];
    fds[0].fd = client_session.socket_fd;
    fds[0].events = POLLIN;

    time_t last_activity = time(NULL);
    const int PING_INTERVAL = 15;

    while (running) {
        // 1. 사용자 키보드 입력 처리 (비동기)
        int ch = getch();
        if (ch != ERR) { // 키 입력이 있었을 경우
            last_activity = time(NULL); // 키 입력도 활동으로 간주
            if (global_ui_manager && global_ui_manager->menu) {
                switch(ch) {
                    case KEY_DOWN: 
                        menu_driver(global_ui_manager->menu, REQ_DOWN_ITEM); 
                        break;
                    case KEY_UP: 
                        menu_driver(global_ui_manager->menu, REQ_UP_ITEM); 
                        break;
                    case 10:       // Enter 키
                    case KEY_ENTER:
                    {
                        ITEM* cur_item = current_item(global_ui_manager->menu);
                        if (!cur_item) break;
                        
                        // 현재 선택된 아이템의 인덱스 찾기
                        int index = 0;
                        for (int i = 0; i < device_count; i++) {
                            if (device_menu_items[i] == cur_item) {
                                index = i;
                                break;
                            }
                        }
                        
                        handle_device_reservation(index);
                        break;
                    }
                    case 27: // Esc 키
                        // 장비 목록 메뉴 닫기
                        if (global_ui_manager->menu) {
                            unpost_menu(global_ui_manager->menu);
                            free_menu(global_ui_manager->menu);
                            global_ui_manager->menu = NULL;
                        }
                        if (global_ui_manager->menu_win) {
                            delwin(global_ui_manager->menu_win);
                            global_ui_manager->menu_win = NULL;
                        }
                        cleanup_device_menu_items();
                        show_main_menu();
                        break;
                }
            }
        }

        // 2. 서버 메시지 및 네트워크 상태 처리
        int ret = poll(fds, 1, 0); // 타임아웃을 0으로 하여 즉시 리턴 (논블로킹)
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("Network", "poll 실패: %s", strerror(errno));
            break; // 루프 종료
        }

        if (ret > 0 && (fds[0].revents & POLLIN)) {
            Message* received_msg = (Message*)malloc(sizeof(Message));
            if (!received_msg) {
                LOG_ERROR("Network", "메시지 메모리 할당 실패");
                running = false;
                break;
            }
            if (receive_message(client_session.ssl, received_msg) < 0) {
                LOG_ERROR("Network", "서버로부터 메시지 수신 실패, 연결이 종료되었을 수 있습니다.");
                free(received_msg);
                running = false;
                break;
            }
            handle_server_message(received_msg);
            last_activity = time(NULL);
            cleanup_message(received_msg);
            free(received_msg);
        }
        
        // 3. 주기적인 작업 처리 (PING 등)
        time_t current_time = time(NULL);
        if (current_time - last_activity >= PING_INTERVAL) {
            Message* ping_msg = create_ping_message();
            if (ping_msg) {
                if (send_message(client_session.ssl, ping_msg) == 0) {
                    last_activity = current_time;
                    LOG_DEBUG("Network", "PING 메시지 전송");
                }
                cleanup_message(ping_msg);
                free(ping_msg);
            }
        }

        // 4. UI 업데이트
        update_client_status(&client_session);
        refresh_all_windows();

        // CPU 과사용 방지를 위한 짧은 대기
        usleep(10000); // 10ms
    }

    // 자원 정리
    cleanup_client_session(&client_session);
    cleanup_performance_stats(perf_stats);
    cleanup_ssl_manager(&ssl_manager);
    cleanup_ui();
    cleanup_logger();

    return 0;
}

static void cleanup_device_menu_items(void) {
    if (device_menu_items) {
        for (int i = 0; device_menu_items[i]; i++) {
            free_item(device_menu_items[i]);
        }
        free(device_menu_items);
        device_menu_items = NULL;
    }
}

static WINDOW* create_menu_window(UIManager* manager, int height) {
    WINDOW* win = newwin(height, 80, 0, 0);
    if (!win) return NULL;
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " 장비 목록 ");
    wrefresh(win);
    return win;
}

// 문자열을 DeviceStatus enum으로 변환하는 함수 추가
static DeviceStatus string_to_device_status(const char* status_str) {
    if (strcmp(status_str, "available") == 0) return DEVICE_AVAILABLE;
    if (strcmp(status_str, "reserved") == 0) return DEVICE_RESERVED;
    if (strcmp(status_str, "maintenance") == 0) return DEVICE_MAINTENANCE;
    return DEVICE_AVAILABLE; // 기본값
}
