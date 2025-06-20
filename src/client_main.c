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
static void client_signal_handler(int signum);
static void client_cleanup_resources(void);
static int client_connect_to_server(const char* server_ip, int port);
static void client_handle_server_message(const message_t* message);
static void client_login_submitted(const char* username, const char* password);

static void client_draw_main_menu(void);
static void client_draw_logged_in_menu(void);
static void client_draw_device_list(void);
static void client_draw_login_input_ui(void);
static void client_handle_keyboard_input(int ch);
static void client_handle_input_main_menu(int ch);
static void client_handle_input_logged_in_menu(int ch);
static void client_handle_input_device_list(int ch);
static void client_handle_input_reservation_time(int ch);
static void client_handle_input_login_input(int ch);
static device_status_t client_string_to_device_status(const char* status_str);
static void client_process_and_store_device_list(const message_t* message);
static void client_draw_message_win(const char* msg);

// 한글/영문 혼용 문자열의 실제 표시 폭 계산
static int get_display_width(const char* str) {
    int width = 0;
    while (*str) {
        unsigned char c = (unsigned char)*str;
        if (c < 0x80) {
            width += 1; // ASCII
            str++;
        } else if ((c & 0xE0) == 0xC0) {
            width += 2; // 2바이트(한글 등)
            str += 2;
        } else if ((c & 0xF0) == 0xE0) {
            width += 2; // 3바이트(한글 등)
            str += 3;
        } else {
            str++;
        }
    }
    return width;
}

// 지정한 폭에 맞춰 문자열을 출력하고, 남는 공간은 공백으로 채움
static void print_fixed_width(WINDOW* win, int y, int x, const char* str, int width) {
    mvwprintw(win, y, x, "%s", str);
    int disp = get_display_width(str);
    for (int i = disp; i < width; ++i) {
        mvwaddch(win, y, x + i, ' ');
    }
}

// 전역 변수
extern ui_manager_t* g_ui_manager;
static client_session_t client_session;
static ssl_manager_t ssl_manager;
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
static device_t* device_list = NULL;
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
    signal(SIGINT, client_signal_handler); signal(SIGTERM, client_signal_handler);
    if (ui_init(UI_CLIENT) < 0 || network_init_ssl_manager(&ssl_manager, false, NULL, NULL) < 0) { client_cleanup_resources(); return 1; }
    if (client_connect_to_server(argv[1], atoi(argv[2])) < 0) { client_cleanup_resources(); return 1; }

    while (running) {
        client_draw_ui_for_current_state();
        
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
            int ch = wgetch(g_ui_manager->menu_win);
            if(ch != ERR) client_handle_keyboard_input(ch);
        }

        if (fds[1].revents & POLLIN) { running = false; }

        if (fds[0].revents & POLLIN) {
            message_t* msg = message_receive(client_session.ssl);
            if (msg) {
                client_handle_server_message(msg);
                message_destroy(msg);
               
            } else {
               ui_show_error_message("서버와의 연결이 끊어졌습니다. 종료합니다.");
                sleep(2); // 메시지를 볼 수 있도록 잠시 대기
                running = false;
            }
        }
    }
    client_cleanup_resources();
    return 0;
}

void client_draw_ui_for_current_state(void) {
    pthread_mutex_lock(&g_ui_manager->mutex);
    werase(g_ui_manager->menu_win);
    curs_set(0);
    switch (current_state) {
        case APP_STATE_LOGIN:
            client_draw_login_input_ui();
            curs_set(1);
            break;
        case APP_STATE_MAIN_MENU:
            client_draw_main_menu();
            break;
        case APP_STATE_LOGGED_IN_MENU:
            client_draw_logged_in_menu();
            break;
        case APP_STATE_DEVICE_LIST:
            client_draw_device_list();
            break;
        case APP_STATE_RESERVATION_TIME:
            client_draw_device_list();
            int menu_win_height, menu_win_width;
            getmaxyx(g_ui_manager->menu_win, menu_win_height, menu_win_width);
            client_draw_message_win("[숫자] 시간 입력  [Enter] 예약  [ESC] 취소");
            mvwprintw(g_ui_manager->menu_win, LINES - 5, 2, "예약할 시간(초) 입력 (1~86400, ESC:취소): %s", reservation_input_buffer);
            char help_msg[128];
            snprintf(help_msg, sizeof(help_msg), "도움말: 1 ~ 86400 사이의 예약 시간(초)을 입력하고 Enter를 누르세요.");
            mvwprintw(g_ui_manager->menu_win, menu_win_height - 2, 2, "%-s", help_msg);
            curs_set(1);
            break;
        case APP_STATE_EXIT:
            break;
        default:
            break;
    }
    box(g_ui_manager->menu_win, 0, 0);
    wrefresh(g_ui_manager->menu_win);
    pthread_mutex_unlock(&g_ui_manager->mutex);
}

static void client_draw_login_input_ui() {
    int menu_win_height, menu_win_width;
    getmaxyx(g_ui_manager->menu_win, menu_win_height, menu_win_width);
    // 안내 메시지 message_win에 출력
    client_draw_message_win("[Tab] 필드 전환  [Enter] 로그인  [ESC] 메인 메뉴");

    // 아이디 필드 그리기
    if (active_login_field == LOGIN_FIELD_USERNAME) wattron(g_ui_manager->menu_win, A_REVERSE);
    mvwprintw(g_ui_manager->menu_win, 3, 4, "아이디  : %-s", login_username_buffer);
    if (active_login_field == LOGIN_FIELD_USERNAME) wattroff(g_ui_manager->menu_win, A_REVERSE);
    // 우측 패딩을 위해 공백 출력
    mvwprintw(g_ui_manager->menu_win, 3, 13 + login_username_pos, " ");


    // 비밀번호 필드 그리기 (마스킹 처리)
    char password_display[sizeof(login_password_buffer)] = {0};
    if (login_password_pos > 0) {
        memset(password_display, '*', login_password_pos);
    }
    
    if (active_login_field == LOGIN_FIELD_PASSWORD) wattron(g_ui_manager->menu_win, A_REVERSE);
    mvwprintw(g_ui_manager->menu_win, 5, 4, "비밀번호: %-s", password_display);
    if (active_login_field == LOGIN_FIELD_PASSWORD) wattroff(g_ui_manager->menu_win, A_REVERSE);
    mvwprintw(g_ui_manager->menu_win, 5, 13 + login_password_pos, " ");

    // 활성 필드에 커서 위치시키기
    if (active_login_field == LOGIN_FIELD_USERNAME) {
        wmove(g_ui_manager->menu_win, 3, 13 + login_username_pos);
    } else {
        wmove(g_ui_manager->menu_win, 5, 13 + login_password_pos);
    }
}


static void client_draw_main_menu() {
    int menu_win_height, menu_win_width;
    getmaxyx(g_ui_manager->menu_win, menu_win_height, menu_win_width);
    // 안내 메시지 message_win에 출력
    client_draw_message_win("[↑↓] 이동  [Enter] 선택  [ESC] 종료");
    const char* items[] = { "로그인", "종료" };
    for (int i = 0; i < 2; i++) {
        if (i == menu_highlight) wattron(g_ui_manager->menu_win, A_REVERSE);
        mvwprintw(g_ui_manager->menu_win, i + 2, 2, " > %s", items[i]);
        if (i == menu_highlight) wattroff(g_ui_manager->menu_win, A_REVERSE);
    }
}

static void client_draw_logged_in_menu() {
    int menu_win_height, menu_win_width;
    getmaxyx(g_ui_manager->menu_win, menu_win_height, menu_win_width);
    // 안내 메시지 message_win에 출력
    client_draw_message_win("[↑↓] 이동  [Enter] 선택  [ESC] 로그아웃");
    const char* items[] = { "장비 현황 조회 및 예약", "로그아웃" };
    for (int i = 0; i < 2; i++) {
        if (i == menu_highlight) wattron(g_ui_manager->menu_win, A_REVERSE);
        mvwprintw(g_ui_manager->menu_win, i + 2, 2, " > %s", items[i]);
        if (i == menu_highlight) wattroff(g_ui_manager->menu_win, A_REVERSE);
    }
}

static void client_draw_device_list() {
    int menu_win_height, menu_win_width;
    getmaxyx(g_ui_manager->menu_win, menu_win_height, menu_win_width);
    client_draw_message_win("[↑↓] 이동  [Enter] 예약/선택  [C] 예약취소  [ESC] 뒤로");

    if (!device_list || device_count == 0) {
        mvwprintw(g_ui_manager->menu_win, 2, 2, "장비 목록을 가져오는 중이거나, 등록된 장비가 없습니다.");
        return;
    }

    time_t current_time = time(NULL);
    const int visible_items = menu_win_height - 5;

    // 각 열의 시작 x좌표와 폭(한글 포함)
    int col_x[6] = {2, 12, 38, 56, 70, 84};
    int col_w[6] = {8, 24, 15, 12, 12, 10};

    // 헤더
    print_fixed_width(g_ui_manager->menu_win, 1, col_x[0], "ID", col_w[0]);
    print_fixed_width(g_ui_manager->menu_win, 1, col_x[1], "이름", col_w[1]);
    print_fixed_width(g_ui_manager->menu_win, 1, col_x[2], "타입", col_w[2]);
    print_fixed_width(g_ui_manager->menu_win, 1, col_x[3], "상태", col_w[3]);
    print_fixed_width(g_ui_manager->menu_win, 1, col_x[4], "예약자", col_w[4]);

    for (int i = 0; i < visible_items; i++) {
        int device_index = scroll_offset + i;
        if (device_index >= device_count) break;
        device_t* current_device = &device_list[device_index];
        if (current_device->status == DEVICE_RESERVED && current_device->reservation_end_time > 0) {
            if (current_time >= current_device->reservation_end_time) {
                current_device->status = DEVICE_AVAILABLE;
                current_device->reserved_by[0] = '\0';
            }
        }
        char status_str[32];
        if (current_device->status == DEVICE_RESERVED) {
            long remaining_sec = (current_device->reservation_end_time > current_time) ? (current_device->reservation_end_time - current_time) : 0;
            snprintf(status_str, sizeof(status_str), "reserved (%lds)", remaining_sec);
        } else {
            strncpy(status_str, message_get_device_status_string(current_device->status), sizeof(status_str) - 1);
            status_str[sizeof(status_str) - 1] = '\0';
        }
        int row = i + 2;
        if (device_index == menu_highlight) wattron(g_ui_manager->menu_win, A_REVERSE);
        print_fixed_width(g_ui_manager->menu_win, row, col_x[0], current_device->id, col_w[0]);
        print_fixed_width(g_ui_manager->menu_win, row, col_x[1], current_device->name, col_w[1]);
        print_fixed_width(g_ui_manager->menu_win, row, col_x[2], current_device->type, col_w[2]);
        print_fixed_width(g_ui_manager->menu_win, row, col_x[3], status_str, col_w[3]);
        print_fixed_width(g_ui_manager->menu_win, row, col_x[4], current_device->reserved_by, col_w[4]);
        if (device_index == menu_highlight) wattroff(g_ui_manager->menu_win, A_REVERSE);
    }

    if (device_count > 0 && menu_highlight < device_count) {
        char help_message[128] = {0};
        device_t* highlighted_device = &device_list[menu_highlight];
        if (highlighted_device->status == DEVICE_RESERVED && 
            strcmp(highlighted_device->reserved_by, client_session.username) == 0) {
            snprintf(help_message, sizeof(help_message), "도움말: 'C' 키를 눌러 직접 예약을 취소할 수 있습니다.");
        } else {
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
        mvwprintw(g_ui_manager->menu_win, menu_win_height - 2, 2, "%-s", help_message);
    }
}

static void client_handle_keyboard_input(int ch) {
    switch (current_state) {
        case APP_STATE_LOGIN:
            client_handle_input_login_input(ch);
            break;
        case APP_STATE_MAIN_MENU:
            client_handle_input_main_menu(ch);
            break;
        case APP_STATE_LOGGED_IN_MENU:
            client_handle_input_logged_in_menu(ch);
            break;
        case APP_STATE_DEVICE_LIST:
            client_handle_input_device_list(ch);
            break;
        case APP_STATE_RESERVATION_TIME:
            client_handle_input_reservation_time(ch);
            break;
        default:
            break;
    }
}

static void client_handle_input_main_menu(int ch) {
    switch (ch) {
        case KEY_UP:
            menu_highlight = (menu_highlight == 0) ? 1 : 0;
            break;
        case KEY_DOWN:
            menu_highlight = (menu_highlight == 1) ? 0 : 1;
            break;
        case 10: // Enter 키
            if (menu_highlight == 0) { // "로그인" 선택
                current_state = APP_STATE_LOGIN;
                menu_highlight = 0;
                active_login_field = LOGIN_FIELD_USERNAME;
                memset(login_username_buffer, 0, sizeof(login_username_buffer));
                memset(login_password_buffer, 0, sizeof(login_password_buffer));
                login_username_pos = 0;
                login_password_pos = 0;
                flushinp(); // ncurses 입력 버퍼를 비워 씹힘 현상을 방지합니다.
            } else { // "종료" 선택
                running = false;
            }
            break;
        case 27: // ESC
            running = false;
            break;
        default:
            break;
    }
}

static void client_handle_input_login_input(int ch) {
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

    // [로그 추가] 어떤 키가 어느 필드에 입력됐는지 기록
    LOG_INFO("LoginInput", "입력 필드: %s, 입력 키: %d (문자: %c)",
        (active_login_field == LOGIN_FIELD_USERNAME) ? "USERNAME" : "PASSWORD", ch, (ch >= 32 && ch <= 126) ? ch : ' ');

    switch (ch) {
        case 9: // Tab 키
            active_login_field = (active_login_field == LOGIN_FIELD_USERNAME) 
                               ? LOGIN_FIELD_PASSWORD 
                               : LOGIN_FIELD_USERNAME;
            break;

        case 27: // Escape 키
            current_state = APP_STATE_MAIN_MENU;
            menu_highlight = 0;
            break;

        case 10: // Enter 키
            if (active_login_field == LOGIN_FIELD_USERNAME) {
                active_login_field = LOGIN_FIELD_PASSWORD; // 아이디 필드에서 Enter -> 비밀번호 필드로 이동
            } else {
                // 비밀번호 필드에서 Enter -> 로그인 시도
                if (login_username_pos > 0 && login_password_pos > 0) {
                    client_login_submitted(login_username_buffer, login_password_buffer);
                } else {
                    ui_show_error_message("아이디와 비밀번호를 모두 입력하세요.");
                }
            }
            break;

        case '\b':
        case KEY_BACKSPACE:
        case 127:
            if (*current_pos > 0) {
                (*current_pos)--;
                current_buffer[*current_pos] = '\0';
                // [로그 추가] 실제로 UI에서 삭제가 반영될 때만 로그
                LOG_INFO("LoginInput", "UI 반영: %s 필드에서 백스페이스(코드:%d)로 1글자 삭제", (active_login_field == LOGIN_FIELD_USERNAME) ? "USERNAME" : "PASSWORD", ch);
            }
            break;

        default:
            // 화면에 표시 가능한 문자인지 확인 후 버퍼에 추가
            if ((ch >= 32 && ch <= 126) && (size_t)(*current_pos) < buffer_size - 1) {
                current_buffer[(*current_pos)++] = ch;
                current_buffer[*current_pos] = '\0';
                // [로그 추가] 실제로 UI에 문자가 추가될 때만 로그
                LOG_INFO("LoginInput", "UI 반영: %s 필드에 키 입력(코드:%d, 문자:%c) 추가", (active_login_field == LOGIN_FIELD_USERNAME) ? "USERNAME" : "PASSWORD", ch, ch);
            }
            break;
    }
}

static void client_handle_input_logged_in_menu(int ch) {
    switch (ch) {
        case KEY_UP: 
            menu_highlight = (menu_highlight == 0) ? 1 : 0; 
            break;
        case KEY_DOWN: 
            menu_highlight = (menu_highlight == 1) ? 0 : 1; 
            break;
        case 10: // Enter 키
            if (menu_highlight == 0) { // "장비 현황 조회 및 예약" 선택
                message_t* msg = message_create(MSG_STATUS_REQUEST, NULL);
                if (msg) {
                    if (network_send_message(client_session.ssl, msg) < 0) {
                        running = false;
                    }
                    message_destroy(msg);
                }
            } else { // "로그아웃" 선택
                message_t* logout_msg = message_create(MSG_LOGOUT, NULL);
                if (logout_msg) {
                    network_send_message(client_session.ssl, logout_msg);
                    message_destroy(logout_msg);
                }
                client_session.state = SESSION_DISCONNECTED;
                memset(client_session.username, 0, sizeof(client_session.username));
                current_state = APP_STATE_MAIN_MENU;
                menu_highlight = 0;
                ui_show_success_message("로그아웃되었습니다.");
            }
            break;
        case 27: // ESC 키로 메인 메뉴 복귀 (로그아웃과 동일하게 처리)
            message_t* logout_msg = message_create(MSG_LOGOUT, NULL);
            if (logout_msg) {
                network_send_message(client_session.ssl, logout_msg);
                message_destroy(logout_msg);
            }
            client_session.state = SESSION_DISCONNECTED;
            memset(client_session.username, 0, sizeof(client_session.username));
            current_state = APP_STATE_MAIN_MENU;
            menu_highlight = 0;
            ui_show_success_message("로그아웃되었습니다.");
            break;
        default:
            break;
    }
}

static void client_handle_input_device_list(int ch) {
    int menu_win_height, menu_win_width;
    getmaxyx(g_ui_manager->menu_win, menu_win_height, menu_win_width);
    (void)menu_win_width;
    const int visible_items = menu_win_height - 5;

    switch (ch) {
        case KEY_UP:
            if (menu_highlight > 0) menu_highlight--;
            if (menu_highlight < scroll_offset) scroll_offset = menu_highlight;
            break;
        case KEY_DOWN:
            if (menu_highlight < device_count - 1) menu_highlight++;
            if (menu_highlight >= scroll_offset + visible_items)
                scroll_offset = menu_highlight - visible_items + 1;
            break;
        case 10: // Enter 키
            if (device_list && menu_highlight < device_count) {
                device_t* dev = &device_list[menu_highlight];
                if (dev->status == DEVICE_AVAILABLE) {
                    reservation_target_device_index = menu_highlight;
                    current_state = APP_STATE_RESERVATION_TIME;
                    reservation_input_pos = 0;
                    memset(reservation_input_buffer, 0, sizeof(reservation_input_buffer));
                    ui_show_success_message("예약 시간을 입력하세요 (초 단위)");
                } else if (dev->status == DEVICE_RESERVED) {
                    if (strcmp(dev->reserved_by, client_session.username) == 0) {
                        ui_show_success_message("이미 예약한 장비입니다.");
                    } else {
                        ui_show_error_message("다른 사용자가 예약한 장비입니다.");
                    }
                } else {
                    ui_show_error_message("점검 중인 장비입니다.");
                }
            }
            break;
        case 'c':
        case 'C':
            if (device_list && menu_highlight < device_count) {
                device_t* dev = &device_list[menu_highlight];
                if (dev->status == DEVICE_RESERVED && strcmp(dev->reserved_by, client_session.username) == 0) {
                    ui_show_success_message("예약 취소 요청 중...");
                    message_t* msg = message_create_cancel(dev->id);
                    if (msg) {
                        if (network_send_message(client_session.ssl, msg) < 0) {
                            running = false;
                        }
                        message_destroy(msg);
                    }
                }
            }
            break;
        case 27: // ESC
            current_state = APP_STATE_LOGGED_IN_MENU;
            menu_highlight = 0;
            break;
    }
}

static void client_handle_input_reservation_time(int ch) {
    switch (ch) {
        case '\n':
        case '\r':
            if (reservation_input_pos > 0) {
                int reservation_time = atoi(reservation_input_buffer);
                if (reservation_time >= 1 && reservation_time <= 86400) {
                    if (reservation_target_device_index >= 0 && reservation_target_device_index < device_count) {
                        device_t* dev = &device_list[reservation_target_device_index];
                        char time_str[32];
                        snprintf(time_str, sizeof(time_str), "%d", reservation_time);
                        message_t* msg = message_create_reservation(dev->id, time_str);
                        if (msg) {
                            if (network_send_message(client_session.ssl, msg) < 0) {
                                running = false;
                            }
                            message_destroy(msg);
                        }
                    }
                    current_state = APP_STATE_DEVICE_LIST;
                    reservation_target_device_index = -1;
                } else {
                    ui_show_error_message("유효하지 않은 시간입니다. (1~86400초)");
                    memset(reservation_input_buffer, 0, sizeof(reservation_input_buffer));
                    reservation_input_pos = 0;
                }
            }
            break;
        case 27: // ESC
            current_state = APP_STATE_DEVICE_LIST;
            reservation_target_device_index = -1;
            break;
        case KEY_BACKSPACE:
        case 127:
            if (reservation_input_pos > 0) {
                reservation_input_buffer[--reservation_input_pos] = '\0';
            }
            break;
        default:
            if (isdigit(ch) && reservation_input_pos < (int)sizeof(reservation_input_buffer) - 1) {
                reservation_input_buffer[reservation_input_pos++] = ch;
                reservation_input_buffer[reservation_input_pos] = '\0';
            }
            break;
    }
}

static void client_handle_server_message(const message_t* message) {
    if (!message) return;
    
    switch (message->type) {
        case MSG_ERROR:
            LOG_WARNING("Client", "서버로부터 에러 메시지 수신: %s", message->data);
            if (strcmp(message->data, "이미 로그인된 사용자입니다.") == 0) {
                LOG_INFO("Client", "이미 로그인된 사용자로 로그인 시도: UI 에러 메시지 표시 및 입력 필드 초기화");
            }
            ui_show_error_message(message->data);
            ui_refresh_all_windows(); // 강제 갱신
            napms(1200); // 1.2초간 메시지 노출
            // 로그인 관련 에러라면 항상 로그인 화면 상태/입력 초기화
            if (current_state == APP_STATE_LOGGED_IN_MENU || current_state == APP_STATE_DEVICE_LIST || current_state == APP_STATE_LOGIN) {
                current_state = APP_STATE_LOGIN;
                menu_highlight = 0;
                active_login_field = LOGIN_FIELD_USERNAME;
                memset(login_username_buffer, 0, sizeof(login_username_buffer));
                memset(login_password_buffer, 0, sizeof(login_password_buffer));
                login_username_pos = 0;
                login_password_pos = 0;
            }
            break;
        
        case MSG_LOGIN:
            if (strcmp(message->data, "success") == 0) {
                LOG_INFO("Client", "로그인 성공 응답 수신");
                // 로그인 성공 시 사용자 정보 설정
                strncpy(client_session.username, login_username_buffer, MAX_USERNAME_LENGTH - 1);
                client_session.username[MAX_USERNAME_LENGTH - 1] = '\0';
                client_session.state = SESSION_LOGGED_IN;
                current_state = APP_STATE_LOGGED_IN_MENU;
                menu_highlight = 0;
                ui_show_success_message("로그인 성공!");
            } else {
                LOG_WARNING("Client", "로그인 실패 응답 수신: %s", message->data);
                ui_show_error_message(message->data);
                // 로그인 실패 시 로그인 화면 상태를 유지합니다.
                LOG_INFO("Client", "로그인 실패로 인해 로그인 화면 상태 유지 (APP_STATE_LOGIN)");
            }
            break;
            
        case MSG_STATUS_RESPONSE:
            client_process_and_store_device_list(message);
            // 장비 목록을 받은 후 UI 상태 업데이트
            if (current_state == APP_STATE_LOGGED_IN_MENU) {
                current_state = APP_STATE_DEVICE_LIST;
                menu_highlight = 0;
                scroll_offset = 0;
            }
            break;
            
        case MSG_RESERVE_RESPONSE:
            ui_show_success_message("예약이 성공적으로 완료되었습니다.");
            break;
        case MSG_CANCEL_RESPONSE:
            ui_show_success_message("예약이 성공적으로 취소되었습니다.");
            break;
        case MSG_STATUS_UPDATE:
            client_process_and_store_device_list(message);
            current_state = APP_STATE_DEVICE_LIST;
            menu_highlight = 0;
            scroll_offset = 0;
            break;
        default:
            break;
    }
}

static void client_process_and_store_device_list(const message_t* message) {
    if (!message) return;
    
    // 기존 장비 목록 해제
    if (device_list) {
        free(device_list);
        device_list = NULL;
    }
    device_count = 0;
    
    // message->args 배열에서 장비 정보 파싱
    int device_count_from_args = message->arg_count / 6;
    if (device_count_from_args > 0) {
        device_list = malloc(device_count_from_args * sizeof(device_t));
        if (!device_list) return;
        
        for (int i = 0; i < device_count_from_args; i++) {
            int base_idx = i * 6;
            if (base_idx + 5 >= message->arg_count) break;
            
            // args 배열에서 장비 정보 추출
            strncpy(device_list[i].id, message->args[base_idx], sizeof(device_list[i].id) - 1);
            device_list[i].id[sizeof(device_list[i].id) - 1] = '\0';
            
            strncpy(device_list[i].name, message->args[base_idx + 1], sizeof(device_list[i].name) - 1);
            device_list[i].name[sizeof(device_list[i].name) - 1] = '\0';
            
            strncpy(device_list[i].type, message->args[base_idx + 2], sizeof(device_list[i].type) - 1);
            device_list[i].type[sizeof(device_list[i].type) - 1] = '\0';
            
            device_list[i].status = client_string_to_device_status(message->args[base_idx + 3]);
            
            device_list[i].reservation_end_time = atol(message->args[base_idx + 4]);
            
            strncpy(device_list[i].reserved_by, message->args[base_idx + 5], sizeof(device_list[i].reserved_by) - 1);
            device_list[i].reserved_by[sizeof(device_list[i].reserved_by) - 1] = '\0';
        }
        device_count = device_count_from_args;
    }
}

static void client_login_submitted(const char* username, const char* password) {
    message_t* login_msg = message_create_login(username, password);
    if (!login_msg) {
        ui_show_error_message("로그인 메시지 생성 실패");
        return;
    }
    
    if (network_send_message(client_session.ssl, login_msg) < 0) {
        ui_show_error_message("로그인 요청 전송 실패");
        message_destroy(login_msg);
        return;
    }
    
    message_destroy(login_msg);
}

static void client_signal_handler(int signum) { (void)signum; (void)write(self_pipe[1], "s", 1); }

static void client_cleanup_resources(void) {
    if (device_list) {
        free(device_list);
        device_list = NULL;
    }
    session_cleanup_client(&client_session);
    network_cleanup_ssl_manager(&ssl_manager);
    ui_cleanup();
    utils_cleanup_logger();
    close(self_pipe[0]);
    close(self_pipe[1]);
}

static int client_connect_to_server(const char* server_ip, int port) {
    client_session.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_session.socket_fd < 0) {
        utils_report_error(ERROR_NETWORK_SOCKET_CREATION_FAILED, "Client", "소켓 생성 실패");
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        utils_report_error(ERROR_NETWORK_IP_CONVERSION_FAILED, "Client", "서버 IP 주소 변환 실패");
        close(client_session.socket_fd);
        return -1;
    }
    
    if (connect(client_session.socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        utils_report_error(ERROR_NETWORK_CONNECT_FAILED, "Client", "서버 연결 실패");
        close(client_session.socket_fd);
        return -1;
    }
    
    ssl_handler_t* ssl_handler = network_perform_ssl_handshake(client_session.socket_fd, &ssl_manager);
    if (!ssl_handler) {
        utils_report_error(ERROR_NETWORK_SSL_HANDSHAKE_FAILED, "Client", "SSL 연결 실패");
        close(client_session.socket_fd);
        return -1;
    }
    
    client_session.ssl = ssl_handler->ssl;
    client_session.state = SESSION_CONNECTING;

    ui_show_success_message("서버에 연결되었습니다. 로그인 정보를 기다립니다...");
    // 연결 확인을 위한 ping 메시지 전송
    message_t* msg = message_create(MSG_PING, NULL);
    if (msg) {
        int ret = network_send_message(client_session.ssl, msg);
        message_destroy(msg);
        if (ret < 0) return -1;
    }
    
    return 0;
}

static device_status_t client_string_to_device_status(const char* status_str) {
    if (!status_str) return DEVICE_MAINTENANCE;
    
    if (strcmp(status_str, "available") == 0) return DEVICE_AVAILABLE;
    if (strcmp(status_str, "reserved") == 0) return DEVICE_RESERVED;
    if (strcmp(status_str, "maintenance") == 0) return DEVICE_MAINTENANCE;
    
    return DEVICE_MAINTENANCE;
}

static void client_draw_message_win(const char* msg) {
    if (!g_ui_manager || !g_ui_manager->message_win) return;
    werase(g_ui_manager->message_win);
    box(g_ui_manager->message_win, 0, 0);
    mvwprintw(g_ui_manager->message_win, 0, 2, "%s", msg);
    wrefresh(g_ui_manager->message_win);
}