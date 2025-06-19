// client_main.c (getmaxyx 매크로 사용법 수정 완료)
/**
 * @file client_main.c
 * @brief 장치 예약 시스템 클라이언트 메인 프로그램 (최종 통합 버전)
 * @details poll() 기반의 단일 이벤트 루프를 사용하여 네트워크, 키보드 입력, 실시간 UI 갱신을
 * 안정적으로 동시에 처리합니다. 모든 알려진 버그가 수정되었습니다.
 */


#include "../include/message.h"
#include "../include/network.h"
#include "../include/session.h"
#include "../include/ui.h"
#include "../include/resource.h"
#include "../include/reservation.h"

// UI 상태를 관리하기 위한 enum
typedef enum {
    UI_STATE_MAIN_MENU,
    UI_STATE_LOGGED_IN_MENU,
    UI_STATE_DEVICE_LIST,
    UI_STATE_INPUT_RESERVATION_TIME,
    UI_STATE_QUITTING
} UIState;

// 함수 프로토타입
static void signal_handler(int signum);
static void cleanup_resources(void);
static int connect_to_server(const char* server_ip, int port);
static void handle_server_message(const Message* message);
static bool handle_login(const char* username, const char* password);
static void draw_ui_for_current_state(void);
static void draw_main_menu(void);
static void draw_logged_in_menu(void);
static void draw_device_list(void);
static void handle_keyboard_input(int ch);
static void handle_input_main_menu(int ch);
static void handle_input_logged_in_menu(int ch);
static void handle_input_device_list(int ch);
static void handle_input_reservation_time(int ch);
static DeviceStatus string_to_device_status(const char* status_str);
static void process_and_store_device_list(const Message* message);

// 전역 변수
extern UIManager* global_ui_manager;
static ClientSession client_session;
static SSLManager ssl_manager;
static bool running = true;
static int self_pipe[2];
static UIState current_state = UI_STATE_MAIN_MENU;
static int menu_highlight = 0;
static int scroll_offset = 0;
static char input_buffer[20] = {0};
static int input_pos = 0;
static int reservation_target_device_index = -1;
static Device* device_list = NULL;
static int device_count = 0;
static bool expecting_network_response = false;

int main(int argc, char* argv[]) {
    if (argc != 3) { 
        error_report(ERROR_INVALID_PARAMETER, "Client", "사용법: %s <서버 IP> <포트>", argv[0]); 
        return 1; 
    }
    if (init_logger("logs/client.log") < 0) return 1;
    if (pipe(self_pipe) == -1) { 
        error_report(ERROR_FILE_OPERATION_FAILED, "Client", "pipe 생성 실패"); 
        return 1; 
    }
    signal(SIGINT, signal_handler); signal(SIGTERM, signal_handler);
    if (init_ui() < 0 || init_ssl_manager(&ssl_manager, false, NULL, NULL) < 0) { cleanup_resources(); return 1; }
    if (connect_to_server(argv[1], atoi(argv[2])) < 0) { cleanup_resources(); return 1; }

    while (running) {
        draw_ui_for_current_state();
        
        struct pollfd fds[3];
        fds[0].fd = client_session.socket_fd;
        fds[0].events = POLLIN;
        fds[1].fd = self_pipe[0];
        fds[1].events = POLLIN;
        fds[2].fd = STDIN_FILENO;
        fds[2].events = POLLIN;

        int ret = poll(fds, 3, 100);
        if (ret < 0) { if (errno == EINTR) continue; break; }

        if (fds[2].revents & POLLIN) {
            int ch = wgetch(global_ui_manager->menu_win);
            if(ch != ERR) handle_keyboard_input(ch);
        }

        if (fds[1].revents & POLLIN) { running = false; }

        if (expecting_network_response && (fds[0].revents & POLLIN)) {
            Message* msg = receive_message(client_session.ssl);
            if (msg) {
                handle_server_message(msg);
                cleanup_message(msg);
                free(msg);
                expecting_network_response = false;
            } else {
                running = false;
            }
        }
    }
    cleanup_resources();
    return 0;
}

static void draw_ui_for_current_state() {
    pthread_mutex_lock(&global_ui_manager->mutex);
    werase(global_ui_manager->menu_win);
    switch (current_state) {
        case UI_STATE_MAIN_MENU: draw_main_menu(); break;
        case UI_STATE_LOGGED_IN_MENU: draw_logged_in_menu(); break;
        case UI_STATE_DEVICE_LIST: draw_device_list(); break;
        case UI_STATE_INPUT_RESERVATION_TIME:
            {
                draw_device_list();
                mvwprintw(global_ui_manager->menu_win, LINES - 5, 2, "예약할 시간(초) 입력 (1~86400, ESC:취소): %s", input_buffer);
                
                int menu_win_height, menu_win_width;
                // [수정] &를 제거한 올바른 매크로 사용법
                getmaxyx(global_ui_manager->menu_win, menu_win_height, menu_win_width);
                (void)menu_win_width;
                char help_msg[128];
                snprintf(help_msg, sizeof(help_msg), "도움말: 1 ~ 86400 사이의 예약 시간(초)을 입력하고 Enter를 누르세요.");
                mvwprintw(global_ui_manager->menu_win, menu_win_height - 2, 2, "%-s", help_msg);
            }
            break;
        case UI_STATE_QUITTING: break;
    }
    box(global_ui_manager->menu_win, 0, 0);
    wrefresh(global_ui_manager->menu_win);
    pthread_mutex_unlock(&global_ui_manager->mutex);
}

static void draw_main_menu() {
    const char* items[] = { "로그인", "종료" };
    mvwprintw(global_ui_manager->menu_win, 0, 2, " 메인 메뉴 ");
    for (int i = 0; i < 2; i++) {
        if (i == menu_highlight) wattron(global_ui_manager->menu_win, A_REVERSE);
        mvwprintw(global_ui_manager->menu_win, i + 2, 2, " > %s", items[i]);
        if (i == menu_highlight) wattroff(global_ui_manager->menu_win, A_REVERSE);
    }
}

static void draw_logged_in_menu() {
    const char* items[] = { "장비 현황 조회 및 예약", "로그아웃" };
    mvwprintw(global_ui_manager->menu_win, 0, 2, " 메인 메뉴 ");
    for (int i = 0; i < 2; i++) {
        if (i == menu_highlight) wattron(global_ui_manager->menu_win, A_REVERSE);
        mvwprintw(global_ui_manager->menu_win, i + 2, 2, " > %s", items[i]);
        if (i == menu_highlight) wattroff(global_ui_manager->menu_win, A_REVERSE);
    }
}

static void draw_device_list() {
    mvwprintw(global_ui_manager->menu_win, 0, 2, " 장비 목록 (Enter: 예약, ESC: 뒤로) ");
    if (!device_list || device_count == 0) {
        mvwprintw(global_ui_manager->menu_win, 2, 2, "장비 목록이 없습니다.");
        return;
    }
    time_t current_time = time(NULL);
    int menu_win_height, menu_win_width;
    getmaxyx(global_ui_manager->menu_win, menu_win_height, menu_win_width);
    (void)menu_win_width;
    const int visible_items = menu_win_height - 5;
    for (int i = 0; i < visible_items; i++) {
        int device_index = scroll_offset + i;
        if (device_index >= device_count) break;
        
        Device* current_device = &device_list[device_index];

        if (current_device->status == DEVICE_RESERVED && current_device->reservation_end_time > 0) {
            if (current_time >= current_device->reservation_end_time) {
                current_device->status = DEVICE_AVAILABLE;
            }
        }
        
        char display_str[256], status_str[128];
        if (current_device->status == DEVICE_RESERVED) {
            long remaining_sec = (current_device->reservation_end_time > current_time) ? (current_device->reservation_end_time - current_time) : 0;
            snprintf(status_str, sizeof(status_str), "reserved by %s (%lds left)", 
                     current_device->reserved_by, remaining_sec);
        } else {
            strncpy(status_str, get_device_status_string(current_device->status), sizeof(status_str) - 1);
            status_str[sizeof(status_str) - 1] = '\0';
        }
        
        snprintf(display_str, sizeof(display_str), "%-10s | %-25s | %-15s | %s",
                 current_device->id, current_device->name, current_device->type, status_str);
                 
        if (device_index == menu_highlight) wattron(global_ui_manager->menu_win, A_REVERSE);
        mvwprintw(global_ui_manager->menu_win, i + 2, 2, " > %s", display_str);
        if (device_index == menu_highlight) wattroff(global_ui_manager->menu_win, A_REVERSE);
    }

    if (device_count > 0 && menu_highlight < device_count) {
        char help_message[128] = {0};
        DeviceStatus highlighted_status = device_list[menu_highlight].status;
        
        switch (highlighted_status) {
            case DEVICE_AVAILABLE:
                snprintf(help_message, sizeof(help_message), "도움말: 예약하려면 Enter 키를 누르세요.");
                break;
            case DEVICE_RESERVED:
                snprintf(help_message, sizeof(help_message), "도움말: '%s'님이 예약중인 장비입니다.", device_list[menu_highlight].reserved_by);
                break;
            case DEVICE_MAINTENANCE:
                snprintf(help_message, sizeof(help_message), "도움말: 점검 중인 장비는 예약할 수 없습니다.");
                break;
            default:
                break; 
        }
        mvwprintw(global_ui_manager->menu_win, menu_win_height - 2, 2, "%-s", help_message);
    }
}

static void handle_keyboard_input(int ch) {
    switch (current_state) {
        case UI_STATE_MAIN_MENU: handle_input_main_menu(ch); break;
        case UI_STATE_LOGGED_IN_MENU: handle_input_logged_in_menu(ch); break;
        case UI_STATE_DEVICE_LIST: handle_input_device_list(ch); break;
        case UI_STATE_INPUT_RESERVATION_TIME: handle_input_reservation_time(ch); break;
        case UI_STATE_QUITTING: break;
    }
}

static void handle_input_main_menu(int ch) {
    switch (ch) {
        case KEY_UP: menu_highlight = (menu_highlight == 0) ? 1 : 0; break;
        case KEY_DOWN: menu_highlight = (menu_highlight == 1) ? 0 : 1; break;
        case 10:
            if (menu_highlight == 0) {
                show_success_message("로그인 시도 중...");
                handle_login("test", "1234");
            } else { running = false; }
            break;
        case 27: running = false; break;
    }
}

static void handle_input_logged_in_menu(int ch) {
    switch (ch) {
        case KEY_UP: menu_highlight = (menu_highlight == 0) ? 1 : 0; break;
        case KEY_DOWN: menu_highlight = (menu_highlight == 1) ? 0 : 1; break;
        case 10:
            if (menu_highlight == 0) {
                Message* msg = create_message(MSG_STATUS_REQUEST, NULL);
                if (msg) {
                    if (send_message(client_session.ssl, msg) < 0) running = false;
                    else expecting_network_response = true;
                    cleanup_message(msg); free(msg);
                }
            } else {
                client_session.state = SESSION_DISCONNECTED;
                current_state = UI_STATE_MAIN_MENU;
                menu_highlight = 0;
                show_success_message("로그아웃되었습니다.");
            }
            break;
        case 27: break;
    }
}

static void handle_input_device_list(int ch) {
    int menu_win_height, menu_win_width;
    getmaxyx(global_ui_manager->menu_win, menu_win_height, menu_win_width);
    (void)menu_win_width;
    const int visible_items = menu_win_height - 5;
    switch (ch) {
        case KEY_UP:
            if (menu_highlight > 0) menu_highlight--;
            if (menu_highlight < scroll_offset) scroll_offset = menu_highlight;
            break;
        case KEY_DOWN:
            if (menu_highlight < device_count - 1) menu_highlight++;
            if (menu_highlight >= scroll_offset + visible_items) scroll_offset = menu_highlight - visible_items + 1;
            break;
        case 10:
            if (device_list && menu_highlight < device_count) {
                if (device_list[menu_highlight].status == DEVICE_AVAILABLE) {
                    reservation_target_device_index = menu_highlight;
                    memset(input_buffer, 0, sizeof(input_buffer));
                    input_pos = 0;
                    current_state = UI_STATE_INPUT_RESERVATION_TIME;
                    flushinp();
                }
            }
            break;
        case 27:
            current_state = UI_STATE_LOGGED_IN_MENU;
            menu_highlight = 0;
            break;
    }
}

static void handle_input_reservation_time(int ch) {
    if ((ch >= '0' && ch <= '9') && (size_t)input_pos < sizeof(input_buffer) - 1) {
        input_buffer[input_pos++] = ch;
        input_buffer[input_pos] = '\0';
    } else if ((ch == KEY_BACKSPACE || ch == 127 || ch == '\b') && input_pos > 0) {
        input_buffer[--input_pos] = '\0';
    } else if (ch == 27) {
        current_state = UI_STATE_DEVICE_LIST;
    } else if (ch == 10) {
        long duration_sec = atol(input_buffer);
        const long MAX_RESERVATION_SECONDS = 86400;

        if (duration_sec > 0 && duration_sec <= MAX_RESERVATION_SECONDS) {
            Message* msg = create_reservation_message(device_list[reservation_target_device_index].id, input_buffer);
            if (msg) {
                if(send_message(client_session.ssl, msg) < 0) running = false;
                else expecting_network_response = true;
                cleanup_message(msg); free(msg);
            }
        } else {
            show_error_message("유효하지 않은 시간입니다. (1~86400초)");
            memset(input_buffer, 0, sizeof(input_buffer));
            input_pos = 0;
        }
    }
}

static void handle_server_message(const Message* message) {
    switch (message->type) {
        case MSG_LOGIN:
            if (strcmp(message->data, "success") == 0 && message->arg_count > 0) {
                client_session.state = SESSION_LOGGED_IN;
                strncpy(client_session.username, message->args[0], MAX_USERNAME_LENGTH - 1);
                client_session.username[MAX_USERNAME_LENGTH - 1] = '\0';
                current_state = UI_STATE_LOGGED_IN_MENU;
                menu_highlight = 0;
                show_success_message("로그인 성공!");
            } else {
                show_error_message(message->data);
            }
            break;
        case MSG_STATUS_RESPONSE:
        case MSG_STATUS_UPDATE:
            process_and_store_device_list(message);
            current_state = UI_STATE_DEVICE_LIST;
            menu_highlight = 0;
            scroll_offset = 0;
            break;
        case MSG_RESERVE_RESPONSE:
            if (strcmp(message->data, "success") == 0) {
                show_success_message("예약 성공! 목록을 갱신합니다...");
                
                Message* msg = create_message(MSG_STATUS_REQUEST, NULL);
                if (msg) {
                    if (send_message(client_session.ssl, msg) < 0) {
                        running = false;
                    } else {
                        expecting_network_response = true;
                    }
                    cleanup_message(msg);
                    free(msg);
                }
            } else {
                show_error_message(message->data);
                current_state = UI_STATE_DEVICE_LIST;
            }
            break;
        case MSG_ERROR:
            show_error_message(message->data);
            break;
        default:
            error_report(ERROR_MESSAGE_INVALID_TYPE, "Message", "알 수 없는 메시지 타입: %d", message->type);
            break;
    }
}

static void process_and_store_device_list(const Message* message) {
    if (device_list) { free(device_list); device_list = NULL; }
    device_count = message->arg_count / 6;
    if (device_count <= 0) return;
    device_list = (Device*)malloc(sizeof(Device) * device_count);
    if (!device_list) { 
        error_report(ERROR_MEMORY_ALLOCATION_FAILED, "Client", "장비 목록 메모리 할당 실패"); 
        device_count = 0; 
        return; 
    }
    for (int i = 0; i < device_count; i++) {
        int base_idx = i * 6;
        strncpy(device_list[i].id, message->args[base_idx], MAX_ID_LENGTH - 1);
        device_list[i].id[MAX_ID_LENGTH - 1] = '\0';
        strncpy(device_list[i].name, message->args[base_idx + 1], MAX_DEVICE_NAME_LENGTH - 1);
        device_list[i].name[MAX_DEVICE_NAME_LENGTH - 1] = '\0';
        strncpy(device_list[i].type, message->args[base_idx + 2], MAX_DEVICE_TYPE_LENGTH - 1);
        device_list[i].type[MAX_DEVICE_TYPE_LENGTH - 1] = '\0';
        device_list[i].status = string_to_device_status(message->args[base_idx + 3]);
        device_list[i].reservation_end_time = (time_t)atol(message->args[base_idx + 4]);
        strncpy(device_list[i].reserved_by, message->args[base_idx + 5], MAX_USERNAME_LENGTH - 1);
        device_list[i].reserved_by[MAX_USERNAME_LENGTH - 1] = '\0';
    }
}

static bool handle_login(const char* username, const char* password) {
    Message* msg = create_login_message(username, password);
    if (!msg) { show_error_message("메시지 생성 실패"); return false; }
    if (send_message(client_session.ssl, msg) < 0) {
        running = false;
    } else {
        expecting_network_response = true;
    }
    cleanup_message(msg);
    free(msg);
    return true;
}

static void signal_handler(int signum) { (void)signum; (void)write(self_pipe[1], "s", 1); }

static void cleanup_resources(void) {
    if (device_list) { free(device_list); device_list = NULL; }
    cleanup_client_session(&client_session);
    cleanup_ssl_manager(&ssl_manager);
    cleanup_ui();
    cleanup_logger();
    close(self_pipe[0]);
    close(self_pipe[1]);
}

static int connect_to_server(const char* server_ip, int port) {
    int sock_fd = init_client_socket(server_ip, port);
    if (sock_fd < 0) return -1;
    SSLHandler* ssl_handler = create_ssl_handler(&ssl_manager, sock_fd);
    if (!ssl_handler || handle_ssl_handshake(ssl_handler) != 0) {
        if(ssl_handler) cleanup_ssl_handler(ssl_handler);
        close(sock_fd);
        return -1;
    }
    client_session.ssl = ssl_handler->ssl;
    client_session.socket_fd = sock_fd;
    client_session.state = SESSION_CONNECTING;
    return 0;
}

static DeviceStatus string_to_device_status(const char* status_str) {
    if (strcmp(status_str, "reserved") == 0) return DEVICE_RESERVED;
    if (strcmp(status_str, "maintenance") == 0) return DEVICE_MAINTENANCE;
    return DEVICE_AVAILABLE;
}