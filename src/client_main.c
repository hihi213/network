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


// 로그인 필드 구분을 위한 enum
typedef enum {
    LOGIN_FIELD_USERNAME,
    LOGIN_FIELD_PASSWORD
} LoginField;


// UI 상태를 관리하기 위한 enum (State Machine 기반)
typedef enum {
    APP_STATE_LOGIN = 0,        // 로그인 화면 (초기 상태)
    APP_STATE_SYNCING,          // [추가] 시간 동기화 중 상태
    APP_STATE_MAIN_MENU,        // 메인 메뉴
    APP_STATE_LOGGED_IN_MENU,   // 로그인된 메뉴
    APP_STATE_DEVICE_LIST,      // 장비 목록
    APP_STATE_RESERVATION_TIME, // 예약 시간 입력
    APP_STATE_EXIT              // 종료
} AppState;

// 메뉴 데이터 정의
static ui_menu_item_t main_menu_items[] = {
    {"로그인", 0, true, NULL},
    {"종료", 1, true, NULL}
};

static ui_menu_item_t logged_in_menu_items[] = {
    {"장비 현황 조회 및 예약", 0, true, NULL},
    {"로그아웃", 1, true, NULL}
};

static ui_menu_t main_menu = {
    NULL, main_menu_items, 2, 0, "[↑↓] 이동  [Enter] 선택  [ESC] 종료"
};

static ui_menu_t logged_in_menu = {
    NULL, logged_in_menu_items, 2, 0, "[↑↓] 이동  [Enter] 선택  [ESC] 로그아웃"
};

// 함수 프로토타입
static void client_signal_handler(int signum);
static void client_cleanup_resources(void);
static int client_connect_to_server(const char* server_ip, int port);
static void client_handle_server_message(const message_t* message);
static void client_login_submitted(const char* username, const char* password);
static void client_perform_logout(void);

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

// 전역 변수
extern ui_manager_t* g_ui_manager;
static client_session_t client_session;
static ssl_manager_t ssl_manager;
static bool running = true;
static int self_pipe[2];
static AppState current_state = APP_STATE_LOGIN;
static int menu_highlight = 0;
static int scroll_offset = 0;
static time_t g_time_offset = 0; // 서버 시간 - 클라이언트 시간
static bool g_time_sync_completed = false; // [추가] 시간 동기화 완료 여부 추적

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

// [추가] 동기화된 시간을 반환하는 헬퍼 함수
static time_t client_get_synced_time(void) {
    return time(NULL) + g_time_offset;
}

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
        struct pollfd fds[3];
        fds[0].fd = client_session.socket_fd;
        fds[0].events = POLLIN;
        fds[1].fd = self_pipe[0];
        fds[1].events = POLLIN;
        fds[2].fd = STDIN_FILENO;
        fds[2].events = POLLIN;

        int ret = poll(fds, 3, 1000);
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
        // 모든 입력(키보드, 네트워크)을 처리한 후,
        // 최종적으로 확정된 상태를 기반으로 UI를 그립니다.
        client_draw_ui_for_current_state();
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
        case APP_STATE_SYNCING:
        {
            const char* sync_message = "서버와 시간을 동기화하는 중입니다...";
            int max_y, max_x;
            getmaxyx(g_ui_manager->menu_win, max_y, max_x);
            mvwprintw(g_ui_manager->menu_win, max_y / 2, (max_x - strlen(sync_message)) / 2, "%s", sync_message);
            client_draw_message_win("잠시만 기다려주세요...");
            break;
        }
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
                int menu_win_height = getmaxy(g_ui_manager->menu_win);
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
    // 메뉴 하이라이트 인덱스 업데이트
    main_menu.highlight_index = menu_highlight;
    
    // 데이터 기반 메뉴 렌더링
    ui_render_menu(g_ui_manager->menu_win, &main_menu);
    
    // 안내 메시지 message_win에 출력
    client_draw_message_win(main_menu.help_text);
}

static void client_draw_logged_in_menu() {
    // 메뉴 하이라이트 인덱스 업데이트
    logged_in_menu.highlight_index = menu_highlight;
    
    // 데이터 기반 메뉴 렌더링
    ui_render_menu(g_ui_manager->menu_win, &logged_in_menu);
    
    // 안내 메시지 message_win에 출력
    client_draw_message_win(logged_in_menu.help_text);
}

static void client_draw_device_list() {
    int menu_win_height = getmaxy(g_ui_manager->menu_win);
    client_draw_message_win("[↑↓] 이동  [Enter] 예약/선택  [C] 예약취소  [ESC] 뒤로");

    if (!device_list || device_count == 0) {
        mvwprintw(g_ui_manager->menu_win, 2, 2, "장비 목록을 가져오는 중이거나, 등록된 장비가 없습니다.");
        return;
    }

    time_t current_time = client_get_synced_time();
    
    // [수정] 클라이언트의 자체적인 만료 처리 로직 제거 - 서버가 보내주는 정보만 신뢰
    // 서버의 상태 업데이트를 그대로 사용하여 UI 일관성 보장

    // 공통 장비 목록 테이블 그리기 함수 사용
    ui_draw_device_table(g_ui_manager->menu_win, device_list, device_count, menu_highlight, true, 
                        NULL, NULL, current_time, true);

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

    // [주석처리] 어떤 키가 어느 필드에 입력됐는지 기록
    // #ifdef DEBUG
    // LOG_INFO("LoginInput", "입력 필드: %s, 입력 키: %d (문자: %c)",
    //     (active_login_field == LOGIN_FIELD_USERNAME) ? "USERNAME" : "PASSWORD", ch, (ch >= 32 && ch <= 126) ? ch : ' ');
    // #endif

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
                // #ifdef DEBUG
                // LOG_INFO("LoginInput", "UI 반영: %s 필드에서 백스페이스(코드:%d)로 1글자 삭제", (active_login_field == LOGIN_FIELD_USERNAME) ? "USERNAME" : "PASSWORD", ch);
                // #endif
            }
            break;

        default:
            // 화면에 표시 가능한 문자인지 확인 후 버퍼에 추가
            if ((ch >= 32 && ch <= 126) && (size_t)(*current_pos) < buffer_size - 1) {
                current_buffer[(*current_pos)++] = ch;
                current_buffer[*current_pos] = '\0';
                // [로그 추가] 실제로 UI에 문자가 추가될 때만 로그
                // #ifdef DEBUG
                // LOG_INFO("LoginInput", "UI 반영: %s 필드에 키 입력(코드:%d, 문자:%c) 추가", (active_login_field == LOGIN_FIELD_USERNAME) ? "USERNAME" : "PASSWORD", ch, ch);
                // #endif
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
                client_perform_logout();
            }
            break;
        case 27: // ESC 키로 메인 메뉴 복귀 (로그아웃과 동일하게 처리)
        {
            client_perform_logout();
        }
        break;
        default:
            break;
    }
}

static void client_handle_input_device_list(int ch) {
    int menu_win_height = getmaxy(g_ui_manager->menu_win);
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
                    LOG_INFO("Client", "예약 시작: 장비=%s, 사용자=%s", dev->id, client_session.username);
                    reservation_target_device_index = menu_highlight;
                    current_state = APP_STATE_RESERVATION_TIME;
                    reservation_input_pos = 0;
                    memset(reservation_input_buffer, 0, sizeof(reservation_input_buffer));
                    ui_show_success_message("예약 시간을 입력하세요 (초 단위)");
                    LOG_INFO("Client", "예약 시간 입력 화면으로 전환: 장비=%s", dev->id);
                } else if (dev->status == DEVICE_RESERVED) {
                    if (strcmp(dev->reserved_by, client_session.username) == 0) {
                        LOG_INFO("Client", "이미 예약한 장비 선택: 장비=%s, 사용자=%s", dev->id, client_session.username);
                        ui_show_success_message("이미 예약한 장비입니다.");
                    } else {
                        LOG_INFO("Client", "다른 사용자가 예약한 장비 선택: 장비=%s, 예약자=%s, 현재사용자=%s", 
                                dev->id, dev->reserved_by, client_session.username);
                        ui_show_error_message("다른 사용자가 예약한 장비입니다.");
                    }
                } else {
                    LOG_INFO("Client", "점검 중인 장비 선택: 장비=%s, 상태=%d", dev->id, dev->status);
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
                } else if (dev->status == DEVICE_RESERVED) {
                    ui_show_error_message("다른 사용자가 예약한 장비입니다.");
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
                LOG_INFO("Client", "예약 시간 입력 완료: 시간=%d초, 입력값=%s", reservation_time, reservation_input_buffer);
                
                if (reservation_time >= 1 && reservation_time <= 86400) {
                    if (reservation_target_device_index >= 0 && reservation_target_device_index < device_count) {
                        device_t* dev = &device_list[reservation_target_device_index];
                        char time_str[32];
                        snprintf(time_str, sizeof(time_str), "%d", reservation_time);
                        
                        LOG_INFO("Client", "예약 요청 전송 시작: 장비=%s, 시간=%d초, 사용자=%s", 
                                dev->id, reservation_time, client_session.username);
                        
                        message_t* msg = message_create_reservation(dev->id, time_str);
                        if (msg) {
                            if (network_send_message(client_session.ssl, msg) < 0) {
                                LOG_ERROR("Client", "예약 요청 전송 실패: 장비=%s, 시간=%d초", dev->id, reservation_time);
                                running = false;
                            } else {
                                LOG_INFO("Client", "예약 요청 전송 성공: 장비=%s, 시간=%d초", dev->id, reservation_time);
                            }
                            message_destroy(msg);
                        } else {
                            LOG_ERROR("Client", "예약 메시지 생성 실패: 장비=%s, 시간=%d초", dev->id, reservation_time);
                        }
                    }
                    current_state = APP_STATE_DEVICE_LIST;
                    reservation_target_device_index = -1;
                    LOG_INFO("Client", "예약 요청 후 장비 목록 화면으로 복귀");
                } else {
                    LOG_WARNING("Client", "유효하지 않은 예약 시간 입력: %d초 (범위: 1~86400)", reservation_time);
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
        {
            LOG_WARNING("Client", "서버로부터 에러 메시지 수신: %s (코드: %d)", message->data, message->error_code);
            
            // 에러 코드에 따른 처리
            switch (message->error_code) {
                case ERROR_SESSION_AUTHENTICATION_FAILED:
                    ui_show_error_message("아이디 또는 비밀번호가 틀립니다.");
                    break;
                case ERROR_SESSION_ALREADY_EXISTS:
                    ui_show_error_message("이미 로그인된 사용자입니다.");
                    // 로그인 화면으로 복귀
                    current_state = APP_STATE_LOGIN;
                    menu_highlight = 0;
                    active_login_field = LOGIN_FIELD_USERNAME;
                    memset(login_username_buffer, 0, sizeof(login_username_buffer));
                    memset(login_password_buffer, 0, sizeof(login_password_buffer));
                    login_username_pos = 0;
                    login_password_pos = 0;
                    break;
                case ERROR_RESOURCE_IN_USE:
                    ui_show_error_message("장비를 사용할 수 없습니다.");
                    break;
                case ERROR_RESOURCE_MAINTENANCE_MODE:
                    ui_show_error_message("점검 중인 장비입니다.");
                    break;
                case ERROR_RESERVATION_ALREADY_EXISTS:
                    ui_show_error_message(message->data); // 서버에서 보낸 상세 메시지 사용
                    break;
                case ERROR_RESERVATION_NOT_FOUND:
                    ui_show_error_message("예약을 찾을 수 없습니다.");
                    break;
                case ERROR_RESERVATION_PERMISSION_DENIED:
                    ui_show_error_message("본인의 예약이 아니므로 취소할 수 없습니다.");
                    break;
                case ERROR_RESERVATION_INVALID_TIME:
                    ui_show_error_message("유효하지 않은 예약 시간입니다.");
                    break;
                case ERROR_UNKNOWN:
                    ui_show_error_message("서버 내부 오류가 발생했습니다.");
                    break;
                case ERROR_NETWORK_CONNECT_FAILED:
                    ui_show_error_message("네트워크 연결 오류가 발생했습니다.");
                    break;
                case ERROR_INVALID_PARAMETER:
                    ui_show_error_message("잘못된 요청입니다.");
                    break;
                case ERROR_SESSION_INVALID_STATE:
                    ui_show_error_message("세션이 만료되었습니다. 다시 로그인해주세요.");
                    current_state = APP_STATE_LOGIN;
                    menu_highlight = 0;
                    active_login_field = LOGIN_FIELD_USERNAME;
                    memset(login_username_buffer, 0, sizeof(login_username_buffer));
                    memset(login_password_buffer, 0, sizeof(login_password_buffer));
                    login_username_pos = 0;
                    login_password_pos = 0;
                    break;
                case ERROR_PERMISSION_DENIED:
                    ui_show_error_message("권한이 없습니다.");
                    break;
                default:
                    ui_show_error_message(message->data); // 기본적으로 서버 메시지 사용
                    break;
            }
            
            ui_refresh_all_windows(); // 강제 갱신
            napms(2000); 
        }
        break;
        
        case MSG_LOGIN:
            if (strcmp(message->data, "success") == 0) {
                LOG_INFO("Client", "로그인 성공 응답 수신");
                // 로그인 성공 시 사용자 정보 설정
                strncpy(client_session.username, login_username_buffer, MAX_USERNAME_LENGTH - 1);
                client_session.username[MAX_USERNAME_LENGTH - 1] = '\0';
                client_session.state = SESSION_LOGGED_IN;
                
                // [수정] 바로 메뉴로 가는 대신, '동기화 중' 상태로 변경
                current_state = APP_STATE_SYNCING;
                
                ui_show_success_message("로그인 성공! 서버와 시간 동기화를 시작합니다.");
                
                // [수정] 시간 동기화 요청 시 클라이언트의 현재 시간(T1)을 인자로 추가
                message_t* sync_msg = message_create(MSG_TIME_SYNC_REQUEST, NULL);
                if (sync_msg) {
                    char t1_str[32];
                    // T1: 클라이언트가 요청을 보내는 시간
                    snprintf(t1_str, sizeof(t1_str), "%ld", time(NULL)); 
                    
                    sync_msg->args[0] = strdup(t1_str);
                    if (sync_msg->args[0]) {
                        sync_msg->arg_count = 1;
                        network_send_message(client_session.ssl, sync_msg);
                    }
                    message_destroy(sync_msg);
                }
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
            LOG_INFO("Client", "서버로부터 예약 성공 응답 수신");
            ui_show_success_message("예약이 성공적으로 완료되었습니다.");
            break;
        case MSG_CANCEL_RESPONSE:
            ui_show_success_message("예약이 성공적으로 취소되었습니다.");
            break;
        case MSG_STATUS_UPDATE:
            // [수정] 서버 업데이트는 항상 신뢰하고 처리 - 초기 상태만 아니라면 항상 업데이트
            if (g_time_sync_completed && current_state > APP_STATE_SYNCING) {
                LOG_INFO("Client", "서버 상태 업데이트 수신: 현재상태=%d", current_state);
                client_process_and_store_device_list(message);
                
                // [수정] 현재 상태가 예약 시간 입력 중이면 상태를 변경하지 않음
                if (current_state != APP_STATE_RESERVATION_TIME) {
                    LOG_INFO("Client", "상태 업데이트로 장비 목록 화면으로 전환");
                    current_state = APP_STATE_DEVICE_LIST;
                } else {
                    LOG_INFO("Client", "예약 시간 입력 중이므로 상태 변경 건너뜀");
                }
                
                // [추가] 장비 목록이 변경되어 현재 선택된 인덱스가 유효하지 않을 경우 처리
                if (menu_highlight >= device_count && device_count > 0) {
                    menu_highlight = device_count - 1;  // 마지막 장비로 조정
                    LOG_INFO("Client", "메뉴 하이라이트 인덱스 조정: %d -> %d", device_count, menu_highlight);
                }
                
                LOG_INFO("Client", "UI 업데이트 완료: 장비수=%d, 하이라이트=%d", device_count, menu_highlight);
            } else {
                // 동기화 중이거나 다른 상태일 때는 로그만 남기고 무시
                LOG_INFO("Client", "Ignoring status update while in state %d (sync_completed: %s)", 
                        current_state, g_time_sync_completed ? "true" : "false");
            }
            break;
        case MSG_TIME_SYNC_RESPONSE:
        {
            if (message->arg_count >= 2) {
                // T4: 클라이언트가 응답을 받은 시간
                time_t t4 = time(NULL); 
                // T1: 클라이언트가 요청을 보냈던 시간 (서버가 되돌려준 값)
                time_t t1 = atol(message->args[0]); 
                // T3: 서버가 응답을 보냈던 시간
                time_t t3 = atol(message->args[1]); 

                // 1. 왕복 시간(RTT) 계산: (응답 받은 시간 - 요청 보낸 시간)
                time_t rtt = t4 - t1;
                // 2. 편도 지연(Latency) 추정: RTT의 절반
                time_t latency = rtt / 2;
                // 3. 현재 실제 서버 시간 추정: 서버가 보낸 시간 + 편도 지연
                time_t actual_server_time = t3 + latency;
                // 4. 최종 시간 오차 계산: (추정된 실제 서버 시간 - 현재 클라이언트 시간)
                g_time_offset = actual_server_time - t4;

                LOG_INFO("TimeSync", "정밀 시간 동기화 완료. RTT: %ld초, Latency: %ld초, Offset: %ld초", rtt, latency, g_time_offset);

                // [추가] 시간 동기화 완료 플래그 설정
                g_time_sync_completed = true;

                // 동기화가 완료되었으므로 로그인 후 메뉴 상태로 안전하게 전환
                current_state = APP_STATE_LOGGED_IN_MENU;
                menu_highlight = 0; // 메뉴 하이라이트 초기화
            } else {
                LOG_WARNING("TimeSync", "시간 동기화 응답 형식이 올바르지 않음");
                // 예외 처리 (단순 동기화 또는 동기화 실패 처리)
                ui_show_error_message("시간 동기화에 실패했습니다.");
                current_state = APP_STATE_LOGGED_IN_MENU;
                menu_highlight = 0;
            }
            break;
        }
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
    int device_count_from_args = message->arg_count / DEVICE_INFO_ARG_COUNT;
    if (device_count_from_args > 0) {
        device_list = malloc(device_count_from_args * sizeof(device_t));
        if (!device_list) return;
        
        for (int i = 0; i < device_count_from_args; i++) {
            int base_idx = i * DEVICE_INFO_ARG_COUNT;
            if (base_idx + (DEVICE_INFO_ARG_COUNT - 1) >= message->arg_count) break;
            
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

static void client_perform_logout(void) {
    message_t* logout_msg = message_create(MSG_LOGOUT, NULL);
    if (logout_msg) {
        network_send_message(client_session.ssl, logout_msg);
        message_destroy(logout_msg);
    }
    client_session.state = SESSION_DISCONNECTED;
    memset(client_session.username, 0, sizeof(client_session.username));
    current_state = APP_STATE_MAIN_MENU;
    menu_highlight = 0;
    // [추가] 시간 동기화 상태 리셋
    g_time_sync_completed = false;
    g_time_offset = 0;
    ui_show_success_message("로그아웃되었습니다.");
}

static void client_signal_handler(int signum) { 
    utils_default_signal_handler(signum, self_pipe[1]); 
}

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
    client_session.socket_fd = network_init_client_socket(server_ip, port);
    if (client_session.socket_fd < 0) {
        utils_report_error(ERROR_NETWORK_CONNECT_FAILED, "Client", "서버 연결 실패");
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