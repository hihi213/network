// client_main.c (입력 씹힘 문제 해결 및 로그인 UI 개선 완료)
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
#include <ctype.h> // isprint() 사용을 위해 추가

// 로그인 필드 구분을 위한 enum
typedef enum {
    LOGIN_FIELD_USERNAME,
    LOGIN_FIELD_PASSWORD
} LoginField;


// UI 상태를 관리하기 위한 enum (State Machine 기반)
typedef enum {
    APP_STATE_LOGIN = 0,        // 로그인 화면 (초기 상태)
    APP_STATE_MAIN_MENU,        // 메인 메뉴
    APP_STATE_LOGGED_IN_MENU,   // 로그인된 메뉴
    APP_STATE_DEVICE_LIST,      // 장비 목록
    APP_STATE_RESERVATION_TIME, // 예약 시간 입력
    APP_STATE_EXIT              // 종료
} AppState;

// 함수 프로토타입
static void signal_handler(int signum);
static void cleanup_resources(void);
static int connect_to_server(const char* server_ip, int port);
static void handle_server_message(const Message* message);
static bool handle_login(const char* username, const char* password);
static void on_login_submitted(const char* username, const char* password);
static void draw_ui_for_current_state(void);
static void draw_main_menu(void);
static void draw_logged_in_menu(void);
static void draw_device_list(void);
static void draw_login_input_ui(void);
static void handle_keyboard_input(int ch);
static void handle_input_main_menu(int ch);
static void handle_input_logged_in_menu(int ch);
static void handle_input_device_list(int ch);
static void handle_input_reservation_time(int ch);
static void handle_input_login_input(int ch);
static DeviceStatus string_to_device_status(const char* status_str);
static void process_and_store_device_list(const Message* message);

// 전역 변수
extern UIManager* global_ui_manager;
static ClientSession client_session;
static SSLManager ssl_manager;
static bool running = true;
static int self_pipe[2];
static AppState current_state = APP_STATE_LOGIN;
static int menu_highlight = 0;
static int scroll_offset = 0;

// 입력 버퍼 분리
static char reservation_input_buffer[20] = {0};
static int reservation_input_pos = 0;
static char login_username_buffer[MAX_USERNAME_LENGTH] = {0};
static int login_username_pos = 0;
static char login_password_buffer[128] = {0}; // 비밀번호 버퍼
static int login_password_pos = 0;
static LoginField active_login_field = LOGIN_FIELD_USERNAME;

static int reservation_target_device_index = -1;
static Device* device_list = NULL;
static int device_count = 0;

int main(int argc, char* argv[]) {
    if (argc != 3) { 
        utils_report_error(ERROR_INVALID_PARAMETER, "Client", "사용법: %s <서버 IP> <포트>", argv[0]); 
        return 1; 
    }
    if (utils_init_logger("logs/client.log") < 0) return 1;
    if (pipe(self_pipe) == -1) { 
        utils_report_error(ERROR_FILE_OPERATION_FAILED, "Client", "pipe 생성 실패"); 
        return 1; 
    }
    signal(SIGINT, signal_handler); signal(SIGTERM, signal_handler);
    if (init_ui() < 0 || network_init_ssl_manager(&ssl_manager, false, NULL, NULL) < 0) { cleanup_resources(); return 1; }
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

        if (fds[0].revents & POLLIN) {
            Message* msg = message_receive(client_session.ssl);
            if (msg) {
                handle_server_message(msg);
                message_destroy(msg);
               
            } else {
               show_error_message("서버와의 연결이 끊어졌습니다. 종료합니다.");
                sleep(2); // 메시지를 볼 수 있도록 잠시 대기
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
    curs_set(0); // 기본적으로 커서 숨김

    switch (current_state) {
        case APP_STATE_LOGIN: 
            LOG_INFO("Client", "UI 그리기: 로그인 화면");
            draw_login_input_ui();
            curs_set(1); // 로그인 화면에서 커서 보임
            break;
        case APP_STATE_MAIN_MENU: 
            LOG_INFO("Client", "UI 그리기: 메인 메뉴 화면");
            draw_main_menu(); 
            break;
        case APP_STATE_LOGGED_IN_MENU: 
            LOG_INFO("Client", "UI 그리기: 로그인된 메뉴 화면");
            draw_logged_in_menu(); 
            break;
        case APP_STATE_DEVICE_LIST: 
            LOG_INFO("Client", "UI 그리기: 장비 목록 화면");
            draw_device_list(); 
            break;
        case APP_STATE_RESERVATION_TIME:
            {
                LOG_INFO("Client", "UI 그리기: 예약 시간 입력 화면");
                draw_device_list();
                mvwprintw(global_ui_manager->menu_win, LINES - 5, 2, "예약할 시간(초) 입력 (1~86400, ESC:취소): %s", reservation_input_buffer);
                
                int menu_win_height, menu_win_width;
                getmaxyx(global_ui_manager->menu_win, menu_win_height, menu_win_width);
                (void)menu_win_width;
                char help_msg[128];
                snprintf(help_msg, sizeof(help_msg), "도움말: 1 ~ 86400 사이의 예약 시간(초)을 입력하고 Enter를 누르세요.");
                mvwprintw(global_ui_manager->menu_win, menu_win_height - 2, 2, "%-s", help_msg);
                curs_set(1); // 예약 시간 입력 시 커서 보임
            }
            break;
        case APP_STATE_EXIT: 
            LOG_INFO("Client", "UI 그리기: 종료 상태");
            break;
        default:
            LOG_WARNING("Client", "알 수 없는 UI 상태: %d", current_state);
            break;
    }
    box(global_ui_manager->menu_win, 0, 0);
    wrefresh(global_ui_manager->menu_win);
    pthread_mutex_unlock(&global_ui_manager->mutex);
}

static void draw_login_input_ui() {
    mvwprintw(global_ui_manager->menu_win, 0, 2, " 로그인 (Tab: 필드 전환, Enter: 로그인, ESC: 뒤로) ");

    // 아이디 필드 그리기
    if (active_login_field == LOGIN_FIELD_USERNAME) wattron(global_ui_manager->menu_win, A_REVERSE);
    mvwprintw(global_ui_manager->menu_win, 3, 4, "아이디  : %-s", login_username_buffer);
    if (active_login_field == LOGIN_FIELD_USERNAME) wattroff(global_ui_manager->menu_win, A_REVERSE);
    // 우측 패딩을 위해 공백 출력
    mvwprintw(global_ui_manager->menu_win, 3, 13 + login_username_pos, " ");


    // 비밀번호 필드 그리기 (마스킹 처리)
    char password_display[sizeof(login_password_buffer)] = {0};
    if (login_password_pos > 0) {
        memset(password_display, '*', login_password_pos);
    }
    
    if (active_login_field == LOGIN_FIELD_PASSWORD) wattron(global_ui_manager->menu_win, A_REVERSE);
    mvwprintw(global_ui_manager->menu_win, 5, 4, "비밀번호: %-s", password_display);
    if (active_login_field == LOGIN_FIELD_PASSWORD) wattroff(global_ui_manager->menu_win, A_REVERSE);
    mvwprintw(global_ui_manager->menu_win, 5, 13 + login_password_pos, " ");

    // 활성 필드에 커서 위치시키기
    if (active_login_field == LOGIN_FIELD_USERNAME) {
        wmove(global_ui_manager->menu_win, 3, 13 + login_username_pos);
    } else {
        wmove(global_ui_manager->menu_win, 5, 13 + login_password_pos);
    }
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
    mvwprintw(global_ui_manager->menu_win, 0, 2, " 장비 목록 (↑↓: 이동, Enter: 예약/선택, C: 예약취소, ESC: 뒤로) ");

    if (!device_list || device_count == 0) {
        mvwprintw(global_ui_manager->menu_win, 2, 2, "장비 목록을 가져오는 중이거나, 등록된 장비가 없습니다.");
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
                current_device->reserved_by[0] = '\0';
            }
        }
        
        char display_str[256], status_str[128];

        if (current_device->status == DEVICE_RESERVED) {
            long remaining_sec = (current_device->reservation_end_time > current_time) ? (current_device->reservation_end_time - current_time) : 0;
            snprintf(status_str, sizeof(status_str), "reserved by %s (%lds left)", 
                     current_device->reserved_by, remaining_sec);
        } else {
            strncpy(status_str, message_get_device_status_string(current_device->status), sizeof(status_str) - 1);
            status_str[sizeof(status_str) - 1] = '\0';
        }
        
        snprintf(display_str, sizeof(display_str), "%-10s | %-25s | %-15s | %s",
                 current_device->id, current_device->name, current_device->type, status_str);
                 
        if (device_index == menu_highlight) {
            wattron(global_ui_manager->menu_win, A_REVERSE);
        }
        mvwprintw(global_ui_manager->menu_win, i + 2, 2, " > %s", display_str);
        if (device_index == menu_highlight) {
            wattroff(global_ui_manager->menu_win, A_REVERSE);
        }
    }

    if (device_count > 0 && menu_highlight < device_count) {
        char help_message[128] = {0};
        Device* highlighted_device = &device_list[menu_highlight];
        
        if (highlighted_device->status == DEVICE_RESERVED && 
            strcmp(highlighted_device->reserved_by, client_session.username) == 0) {
            snprintf(help_message, sizeof(help_message), "도움말: 'C' 키를 눌러 직접 예약을 취소할 수 있습니다.");
        } 
        else {
            switch (highlighted_device->status) {
                case DEVICE_AVAILABLE:
                    snprintf(help_message, sizeof(help_message), "도움말: 예약하려면 Enter 키를 누르세요.");
                    break;
                case DEVICE_RESERVED:
                    snprintf(help_message, sizeof(help_message), "도움말: '%s'님이 예약중인 장비입니다.", highlighted_device->reserved_by);
                    break;
                case DEVICE_MAINTENANCE:
                    snprintf(help_message, sizeof(help_message), "도움말: 점검 중인 장비는 예약할 수 없습니다.");
                    break;
                default:
                    break; 
            }
        }
        mvwprintw(global_ui_manager->menu_win, menu_win_height - 2, 2, "%-s", help_message);
    }
}


static void handle_keyboard_input(int ch) {
    switch (current_state) {
        case APP_STATE_LOGIN: handle_input_login_input(ch); break;
        case APP_STATE_MAIN_MENU: handle_input_main_menu(ch); break;
        case APP_STATE_LOGGED_IN_MENU: handle_input_logged_in_menu(ch); break;
        case APP_STATE_DEVICE_LIST: handle_input_device_list(ch); break;
        case APP_STATE_RESERVATION_TIME: handle_input_reservation_time(ch); break;
        case APP_STATE_EXIT: break;
    }
}

static void handle_input_main_menu(int ch) {
    switch (ch) {
        case KEY_UP: menu_highlight = (menu_highlight == 0) ? 1 : 0; break;
        case KEY_DOWN: menu_highlight = (menu_highlight == 1) ? 0 : 1; break;
        case 10: // Enter 키
            if (menu_highlight == 0) { // "로그인" 선택
                current_state = APP_STATE_LOGIN;
                active_login_field = LOGIN_FIELD_USERNAME;
                memset(login_username_buffer, 0, sizeof(login_username_buffer));
                login_username_pos = 0;
                memset(login_password_buffer, 0, sizeof(login_password_buffer));
                login_password_pos = 0;
                menu_highlight = 0;
                flushinp(); // ncurses 입력 버퍼를 비워 씹힘 현상을 방지합니다.
            } else { // "종료" 선택
                running = false; 
            }
            break;
        case 27: running = false; break;
    }
}

static void handle_input_login_input(int ch) {
    char* current_buffer = NULL;
    int* current_pos = NULL;
    size_t buffer_size = 0;

    if (active_login_field == LOGIN_FIELD_USERNAME) {
        current_buffer = login_username_buffer;
        current_pos = &login_username_pos;
        buffer_size = sizeof(login_username_buffer);
    } else {
        current_buffer = login_password_buffer;
        current_pos = &login_password_pos;
        buffer_size = sizeof(login_password_buffer);
    }

    switch (ch) {
        case 10: // Enter 키
            if (active_login_field == LOGIN_FIELD_USERNAME) {
                active_login_field = LOGIN_FIELD_PASSWORD; // 아이디 필드에서 Enter -> 비밀번호 필드로 이동
                LOG_INFO("Client", "로그인 입력: 아이디 필드에서 비밀번호 필드로 이동");
            } else {
                // 비밀번호 필드에서 Enter -> 로그인 시도
                if (login_username_pos > 0 && login_password_pos > 0) {
                    LOG_INFO("Client", "로그인 시도: 사용자='%s'", login_username_buffer);
                    show_success_message("로그인 시도 중...");
                    on_login_submitted(login_username_buffer, login_password_buffer);
                } else {
                    LOG_WARNING("Client", "로그인 시도 실패: 아이디 또는 비밀번호가 비어있음");
                    show_error_message("아이디와 비밀번호를 모두 입력하세요.");
                }
            }
            break;

        case 9: // Tab 키
            active_login_field = (active_login_field == LOGIN_FIELD_USERNAME) 
                               ? LOGIN_FIELD_PASSWORD 
                               : LOGIN_FIELD_USERNAME;
            LOG_INFO("Client", "로그인 입력: Tab 키로 필드 전환 (현재 필드: %s)", 
                    active_login_field == LOGIN_FIELD_USERNAME ? "아이디" : "비밀번호");
            break;

        case 27: // Escape 키
            LOG_INFO("Client", "로그인 입력: ESC 키로 메인 메뉴로 복귀");
            current_state = APP_STATE_MAIN_MENU;
            menu_highlight = 0;
            break;

        case KEY_BACKSPACE:
        case 127:
        case '\b':
            if (*current_pos > 0) {
                current_buffer[--(*current_pos)] = '\0';
            }
            break;

        default:
            // 화면에 표시 가능한 문자인지 확인 후 버퍼에 추가
            if (isprint(ch) && (size_t)(*current_pos) < buffer_size - 1) {
                current_buffer[(*current_pos)++] = ch;
                current_buffer[*current_pos] = '\0';
            }
            break;
    }
}


static void handle_input_logged_in_menu(int ch) {
    switch (ch) {
        case KEY_UP: 
            menu_highlight = (menu_highlight == 0) ? 1 : 0; 
            break;
        case KEY_DOWN: 
            menu_highlight = (menu_highlight == 1) ? 0 : 1; 
            break;
        case 10: // Enter 키
            if (menu_highlight == 0) { // "장비 목록" 선택
                LOG_INFO("Client", "장비 목록 요청 전송");
                Message* msg = message_create(MSG_STATUS_REQUEST, NULL);
                if (msg) {
                    if(network_send_message(client_session.ssl, msg) < 0) running = false;
                    message_destroy(msg);
                }
            } else { // "로그아웃" 선택
                LOG_INFO("Client", "로그아웃 처리");
                client_session.state = SESSION_DISCONNECTED;
                current_state = APP_STATE_MAIN_MENU;
                menu_highlight = 0;
                show_success_message("로그아웃되었습니다.");
            }
            break;
        case 27: // Escape 키
            LOG_INFO("Client", "ESC 키로 메인 메뉴로 복귀");
            current_state = APP_STATE_MAIN_MENU;
            menu_highlight = 0;
            break;
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
        case 10: // Enter 키
            if (device_list && menu_highlight < device_count) {
                Device* dev = &device_list[menu_highlight];
                if (dev->status == DEVICE_AVAILABLE) {
                    LOG_INFO("Client", "장비 예약 시작: %s", dev->id);
                    reservation_target_device_index = menu_highlight;
                    current_state = APP_STATE_RESERVATION_TIME;
                    reservation_input_pos = 0;
                    memset(reservation_input_buffer, 0, sizeof(reservation_input_buffer));
                    show_success_message("예약 시간을 입력하세요 (초 단위)");
                } else if (dev->status == DEVICE_RESERVED) {
                    if (strcmp(dev->reserved_by, client_session.username) == 0) {
                        show_success_message("이미 예약한 장비입니다.");
                    } else {
                        show_error_message("다른 사용자가 예약한 장비입니다.");
                    }
                } else {
                    show_error_message("점검 중인 장비입니다.");
                }
            }
            break;
        case 'c':
        case 'C':
            if (device_list && menu_highlight < device_count) {
                Device* dev = &device_list[menu_highlight];
                if (dev->status == DEVICE_RESERVED && strcmp(dev->reserved_by, client_session.username) == 0) {
                    show_success_message("예약 취소 요청 중...");
                    Message* msg = message_create(MSG_CANCEL_REQUEST, NULL);
                    if (msg) {
                        msg->args[0] = strdup(dev->id);
                        msg->arg_count = 1;
                        if(network_send_message(client_session.ssl, msg) < 0) running = false;
                        message_destroy(msg);
                    }
                }
            }
            break;
        case 27: // Escape 키
            current_state = APP_STATE_LOGGED_IN_MENU;
            menu_highlight = 0;
            break;
    }
}

static void handle_input_reservation_time(int ch) {
    if ((ch >= '0' && ch <= '9') && (size_t)reservation_input_pos < sizeof(reservation_input_buffer) - 1) {
        reservation_input_buffer[reservation_input_pos++] = ch;
        reservation_input_buffer[reservation_input_pos] = '\0';
    } else if ((ch == KEY_BACKSPACE || ch == 127 || ch == '\b') && reservation_input_pos > 0) {
        reservation_input_buffer[--reservation_input_pos] = '\0';
    } else if (ch == 27) {
        current_state = APP_STATE_DEVICE_LIST;
    } else if (ch == 10) {
        long duration_sec = atol(reservation_input_buffer);
        const long MAX_RESERVATION_SECONDS = 86400;

        if (duration_sec > 0 && duration_sec <= MAX_RESERVATION_SECONDS) {
            Message* msg = message_create_reservation(device_list[reservation_target_device_index].id, reservation_input_buffer);
            if (msg) {
                if(network_send_message(client_session.ssl, msg) < 0) running = false;
                message_destroy(msg);
            }
        } else {
            show_error_message("유효하지 않은 시간입니다. (1~86400초)");
            memset(reservation_input_buffer, 0, sizeof(reservation_input_buffer));
            reservation_input_pos = 0;
        }
    }
}

static void handle_server_message(const Message* message) {
    switch (message->type) {
        case MSG_ERROR:
        LOG_WARNING("Client", "서버로부터 에러 메시지 수신: %s", message->data);
        show_error_message(message->data);
        // [수정] 로그인 시도 중에 발생한 에러라면, 다시 로그인 입력 화면으로 돌려보냄
        if (current_state == APP_STATE_LOGGED_IN_MENU || current_state == APP_STATE_DEVICE_LIST) {
            // 이 경우는 이미 로그인 된 사용자가 다른 오류를 받은 경우이므로,
            // 상태를 유지하거나 다른 적절한 처리 가능
            LOG_INFO("Client", "이미 로그인된 상태에서 에러 수신, 현재 상태 유지");
        } else {
            // 로그인 시도 중 에러(중복 로그인, 인증 실패 등)가 발생한 경우
            // UI 상태를 다시 로그인 화면으로 명확하게 설정
            LOG_INFO("Client", "로그인 실패로 인해 로그인 화면으로 복귀 (현재 상태: %d -> APP_STATE_LOGIN)", current_state);
            current_state = APP_STATE_LOGIN;
        }
        break;
        case MSG_LOGIN:
            if (strcmp(message->data, "success") == 0 && message->arg_count > 0) {
                LOG_INFO("Client", "로그인 성공: 사용자='%s'", message->args[0]);
                client_session.state = SESSION_LOGGED_IN;
                strncpy(client_session.username, message->args[0], MAX_USERNAME_LENGTH - 1);
                client_session.username[MAX_USERNAME_LENGTH - 1] = '\0';
                LOG_INFO("Client", "UI 상태 변경: 로그인 화면 -> 로그인된 메뉴 (APP_STATE_LOGIN -> APP_STATE_LOGGED_IN_MENU)");
                current_state = APP_STATE_LOGGED_IN_MENU;
                menu_highlight = 0;
                show_success_message("로그인 성공!");
            } else {
                LOG_WARNING("Client", "로그인 실패: %s", message->data);
                show_error_message(message->data);
                // 로그인 실패 시 로그인 화면 상태를 유지합니다.
                LOG_INFO("Client", "로그인 실패로 인해 로그인 화면 상태 유지 (APP_STATE_LOGIN)");
                current_state = APP_STATE_LOGIN;
            }
            break;
        case MSG_STATUS_RESPONSE:
        case MSG_STATUS_UPDATE:
            LOG_INFO("Client", "장비 상태 응답 수신: 장비 수=%d개", message->arg_count / 6);
            process_and_store_device_list(message);
            LOG_INFO("Client", "UI 상태 변경: 로그인된 메뉴 -> 장비 목록 (APP_STATE_LOGGED_IN_MENU -> APP_STATE_DEVICE_LIST)");
            current_state = APP_STATE_DEVICE_LIST;
            menu_highlight = 0;
            scroll_offset = 0;
            break;
        case MSG_RESERVE_RESPONSE:
            if (strcmp(message->data, "success") == 0) {
                LOG_INFO("Client", "예약 성공 응답 수신");
                show_success_message("예약이 성공적으로 완료되었습니다.");
            } else {
                LOG_WARNING("Client", "예약 실패 응답 수신: %s", message->data);
                show_error_message(message->data);
            }
            break;
        case MSG_CANCEL_RESPONSE:
            if (strcmp(message->data, "success") == 0) {
                LOG_INFO("Client", "예약 취소 성공 응답 수신");
                show_success_message("예약이 성공적으로 취소되었습니다.");
            } else {
                LOG_WARNING("Client", "예약 취소 실패 응답 수신: %s", message->data);
                show_error_message(message->data);
            }
            break;
        default:
            // LOG_WARNING("Client", "알 수 없는 메시지 타입: %d", message->type);
            break;
    }
}

static void process_and_store_device_list(const Message* message) {
    if (device_list) { free(device_list); device_list = NULL; }
    device_count = message->arg_count / 6;
    if (device_count <= 0) return;
    device_list = (Device*)malloc(sizeof(Device) * device_count);
    if (!device_list) { 
        utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "Client", "장비 목록 메모리 할당 실패"); 
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
    Message* msg = message_create_login(username, password);
    if (!msg) { show_error_message("메시지 생성 실패"); return false; }
    if (network_send_message(client_session.ssl, msg) < 0) {
        running = false;
    }
    message_destroy(msg);
    return true;
}

static void on_login_submitted(const char* username, const char* password) {
    LOG_INFO("Client", "로그인 제출 처리 시작: 사용자='%s'", username);
    
    // 1) 로그인 메시지 생성 및 전송
    Message* login_msg = message_create_login(username, password);
    if (!login_msg) {
        LOG_WARNING("Client", "로그인 메시지 생성 실패");
        show_error_message("로그인 메시지 생성 실패");
        return;
    }
    
    // 2) 서버에 전송
    if (network_send_message(client_session.ssl, login_msg) < 0) {
        LOG_WARNING("Client", "로그인 요청 전송 실패");
        show_error_message("로그인 요청 전송 실패");
        message_destroy(login_msg);
        return;
    }
    
    LOG_INFO("Client", "로그인 요청 전송 완료, 서버 응답 대기 중...");
    message_destroy(login_msg);
    
    // 3) 서버 응답은 handle_server_message에서 처리됨
    // 로그인 성공 시: current_state = APP_STATE_LOGGED_IN_MENU
    // 로그인 실패 시: current_state = APP_STATE_LOGIN (로그인 화면 유지)
}

static void signal_handler(int signum) { (void)signum; (void)write(self_pipe[1], "s", 1); }

static void cleanup_resources(void) {
    if (device_list) { free(device_list); device_list = NULL; }
    session_cleanup_client(&client_session);
    network_cleanup_ssl_manager(&ssl_manager);
    cleanup_ui();
    utils_cleanup_logger();
    close(self_pipe[0]);
    close(self_pipe[1]);
}

static int connect_to_server(const char* server_ip, int port) {
    int fd = network_init_client_socket(server_ip, port);
    if (fd < 0) return -1;
    
    SSLHandler* h = network_perform_ssl_handshake(fd, &ssl_manager);
    if (!h) return -1;
    
    client_session.ssl = h->ssl;
    client_session.socket_fd = fd;
    client_session.state = SESSION_CONNECTING;

    show_success_message("서버에 연결되었습니다. 로그인 정보를 기다립니다...");
    // 연결 확인을 위한 ping 메시지 전송
    Message* msg = message_create(MSG_PING, NULL);
    if(msg) {
        if(network_send_message(client_session.ssl, msg) < 0) {
            message_destroy(msg);
            return -1;
        }
        message_destroy(msg);
    }
    
    return 0;
}

static DeviceStatus string_to_device_status(const char* status_str) {
    if (strcmp(status_str, "reserved") == 0) return DEVICE_RESERVED;
    if (strcmp(status_str, "maintenance") == 0) return DEVICE_MAINTENANCE;
    return DEVICE_AVAILABLE;
}