/**
 * @file client_main.c
 * @brief 클라이언트 애플리케이션 메인 진입점 - 상태 머신 기반 사용자 인터페이스
 * 
 * @details
 * 이 모듈은 장비 예약 시스템의 클라이언트 애플리케이션 메인 진입점입니다:
 * 
 * **핵심 역할:**
 * - 상태 머신 기반 사용자 인터페이스 관리 (로그인, 메뉴, 장비 목록 등)
 * - 서버와의 SSL/TLS 보안 통신 처리
 * - 사용자 입력을 이벤트 루프 내에서 처리
 * - 실시간 서버 상태 업데이트 수신 및 표시
 * 
 * **시스템 아키텍처에서의 위치:**
 * - 애플리케이션 계층: 클라이언트 최상위 진입점
 * - 네트워크 계층: SSL/TLS 클라이언트 통신
 * - UI 계층: ncurses 기반 터미널 사용자 인터페이스
 * - 비즈니스 로직 계층: 사용자 요청 처리 및 응답 관리
 * 
 * **주요 특징:**
 * - 상태 머신으로 UI 흐름 제어 (LOGIN → MENU → EQUIPMENT → RESERVATION)
 * - 비동기 메시지 처리로 UI 응답성 보장
 * - 실시간 서버 브로드캐스트 메시지 수신
 * - 안전한 연결 종료 및 리소스 정리
 * 
 * **상태 전이:**
 * - LOGIN: 사용자 인증 및 서버 연결
 * - MENU: 메인 메뉴 및 기능 선택
 * - EQUIPMENT: 장비 목록 조회 및 상태 확인
 * - RESERVATION: 예약 생성, 조회, 취소
 * - EXIT: 안전한 종료 처리
 * 
 * @note 클라이언트는 사용자와 서버 간의 중재자 역할을 하며,
 *       직관적인 인터페이스를 통해 복잡한 서버 로직을 추상화합니다.
 */

#include "../include/message.h"
#include "../include/network.h"
#include "../include/session.h"
#include "../include/ui.h"
#include "../include/resource.h"
#include "../include/reservation.h"

/**
 * @brief 로그인 화면의 입력 필드 구분
 * @details Tab 키로 사용자명과 비밀번호 필드를 전환할 수 있습니다.
 */
typedef enum {
    LOGIN_FIELD_USERNAME,  ///< 사용자명 입력 필드
    LOGIN_FIELD_PASSWORD   ///< 비밀번호 입력 필드
} LoginField;

/**
 * @brief 애플리케이션 상태 머신 정의
 * @details
 * 각 상태는 특정 UI 화면과 입력 처리 로직을 가집니다:
 * - LOGIN: 사용자 인증 화면
 * - SYNCING: 서버와의 시간 동기화
 * - MAIN_MENU: 로그인 전 메인 메뉴
 * - LOGGED_IN_MENU: 로그인 후 메뉴
 * - DEVICE_LIST: 장비 목록 조회
 * - RESERVATION_TIME: 예약 시간 입력
 * - EXIT: 프로그램 종료
 */
typedef enum {
    APP_STATE_LOGIN = 0,        // 로그인 화면 (초기 상태)
    APP_STATE_SYNCING,          // 시간 동기화 중 상태
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
static void client_draw_message_win(const char* msg);
static void client_handle_keyboard_input(int ch);
static void client_process_and_store_device_list(const message_t* message);
static device_status_t client_string_to_device_status(const char* status_str);

// 전역 변수
extern ui_manager_t* g_ui_manager;
static client_session_t client_session;
static ssl_manager_t ssl_manager;
static bool running = true;
static int self_pipe[2];  // 시그널 처리를 위한 파이프
static AppState current_state = APP_STATE_LOGIN;
static int menu_highlight = 0;
static int scroll_offset = 0;
static time_t g_time_offset = 0;  // 서버 시간 - 클라이언트 시간 (시간 동기화용)
static bool g_time_sync_completed = false;  // 시간 동기화 완료 여부

// 입력 버퍼 관리
static char reservation_input_buffer[20] = {0};  // 예약 시간 입력 버퍼
static int reservation_input_pos = 0;
static char login_username_buffer[MAX_USERNAME_LENGTH] = {0};  // 로그인 사용자명 버퍼
static int login_username_pos = 0;
static char login_password_buffer[128] = {0};  // 로그인 비밀번호 버퍼
static int login_password_pos = 0;
static LoginField active_login_field = LOGIN_FIELD_USERNAME;

// 장비 목록 관리
static int reservation_target_device_index = -1;  // 예약 대상 장비 인덱스
static device_t* device_list = NULL;  // 장비 목록 배열
static int device_count = 0;  // 장비 개수

/**
 * @brief 동기화된 시간을 반환하는 헬퍼 함수
 * @details 서버와의 시간 차이를 보정하여 정확한 시간을 제공합니다.
 * 
 * @return 서버와 동기화된 현재 시간
 */
static time_t client_get_synced_time(void) {
    return time(NULL) + g_time_offset;
}

/**
 * @brief 클라이언트 메인 함수
 * @details
 * 애플리케이션의 진입점으로 다음 순서로 초기화를 수행합니다:
 * 1. 명령행 인자 검증
 * 2. 로깅 시스템 초기화
 * 3. 시그널 핸들러 설정
 * 4. UI 시스템 초기화
 * 5. SSL 관리자 초기화
 * 6. 서버 연결
 * 7. 이벤트 루프 시작
 * 
 * @param argc 명령행 인자 개수
 * @param argv 명령행 인자 배열
 * @return 성공 시 0, 실패 시 1
 */
int main(int argc, char* argv[]) {
    if (argc != 3) { 
        utils_report_error(ERROR_INVALID_PARAMETER, "Client", "사용법: %s <서버 IP> <포트>", argv[0]); 
        return 1; 
    }
    
    // 로깅 시스템 초기화
    if (utils_init_logger("logs/client.log") < 0) return 1;
    
    // 시그널 처리를 위한 파이프 생성
    if (pipe(self_pipe) == -1) { 
        utils_report_error(ERROR_FILE_OPERATION_FAILED, "Client", "pipe 생성 실패"); 
        return 1; 
    }
    
    // 시그널 핸들러 설정
    signal(SIGINT, client_signal_handler);
    signal(SIGTERM, client_signal_handler);
    
    // UI 시스템 초기화
    if (ui_init(UI_CLIENT) < 0) { 
        return 1; 
    } 

    // 연결 중 메시지 표시 (사용자 피드백 개선)
    pthread_mutex_lock(&g_ui_manager->mutex);
    werase(g_ui_manager->menu_win);
    box(g_ui_manager->menu_win, 0, 0);
    const char* conn_msg = "서버에 연결 중입니다...";
    int max_y, max_x;
    getmaxyx(g_ui_manager->menu_win, max_y, max_x);
    mvwprintw(g_ui_manager->menu_win, max_y / 2, (max_x - strlen(conn_msg)) / 2, "%s", conn_msg);
    wrefresh(g_ui_manager->menu_win);
    pthread_mutex_unlock(&g_ui_manager->mutex);

    // SSL 초기화 및 서버 연결
    if (network_init_ssl_manager(&ssl_manager, false, NULL, NULL) < 0) { 
        client_cleanup_resources(); 
        return 1; 
    }
    if (client_connect_to_server(argv[1], atoi(argv[2])) < 0) { 
        client_cleanup_resources(); 
        return 1; 
    }

    // 메인 이벤트 루프
    while (running) {
        struct pollfd fds[3];
        fds[0].fd = client_session.socket_fd;  // 네트워크 소켓
        fds[0].events = POLLIN;
        fds[1].fd = self_pipe[0];  // 시그널 처리 파이프
        fds[1].events = POLLIN;
        fds[2].fd = STDIN_FILENO;  // 키보드 입력
        fds[2].events = POLLIN;

        // 1초 타임아웃으로 poll 호출
        int ret = poll(fds, 3, 1000);
        if (ret < 0) { 
            if (errno == EINTR) continue; 
            break; 
        }

        // 키보드 입력 처리
        if (fds[2].revents & POLLIN) {
            int ch = wgetch(g_ui_manager->menu_win);
            if(ch != ERR) client_handle_keyboard_input(ch);
        }

        // 시그널 처리
        if (fds[1].revents & POLLIN) { 
            running = false; 
        }

        // 네트워크 메시지 처리
        if (fds[0].revents & POLLIN) {
            message_t* msg = message_receive(client_session.ssl);
            if (msg) {
                client_handle_server_message(msg);
                message_destroy(msg);
            } else {
                ui_show_error_message("서버와의 연결이 끊어졌습니다. 종료합니다.");
                sleep(2);
                running = false;
            }
        }
        
        // UI 상태에 따른 화면 갱신
        client_draw_ui_for_current_state();
    }
    
    client_cleanup_resources();
    return 0;
}

/**
 * @brief 현재 상태에 따른 UI 그리기
 * @details
 * 상태 머신의 현재 상태에 따라 적절한 UI를 렌더링합니다.
 * 각 상태는 고유한 화면 레이아웃과 입력 처리 방식을 가집니다.
 */
void client_draw_ui_for_current_state(void) {
    pthread_mutex_lock(&g_ui_manager->mutex);
    werase(g_ui_manager->menu_win);
    curs_set(0);
    
    switch (current_state) {
        case APP_STATE_LOGIN: {
            // 로그인 화면 UI
            client_draw_message_win("[Tab] 필드 전환  [Enter] 로그인  [ESC] 메인 메뉴");
            
            // 사용자명 필드 (활성화 시 하이라이트)
            if (active_login_field == LOGIN_FIELD_USERNAME) wattron(g_ui_manager->menu_win, A_REVERSE);
            mvwprintw(g_ui_manager->menu_win, 3, 4, "아이디  : %-s", login_username_buffer);
            if (active_login_field == LOGIN_FIELD_USERNAME) wattroff(g_ui_manager->menu_win, A_REVERSE);
            mvwprintw(g_ui_manager->menu_win, 3, 13 + login_username_pos, " ");
            
            // 비밀번호 필드 (마스킹 처리)
            char password_display[sizeof(login_password_buffer)] = {0};
            if (login_password_pos > 0) {
                memset(password_display, '*', login_password_pos);
            }
            if (active_login_field == LOGIN_FIELD_PASSWORD) wattron(g_ui_manager->menu_win, A_REVERSE);
            mvwprintw(g_ui_manager->menu_win, 5, 4, "비밀번호: %-s", password_display);
            if (active_login_field == LOGIN_FIELD_PASSWORD) wattroff(g_ui_manager->menu_win, A_REVERSE);
            mvwprintw(g_ui_manager->menu_win, 5, 13 + login_password_pos, " ");
            
            // 커서 위치 설정
            if (active_login_field == LOGIN_FIELD_USERNAME) {
                wmove(g_ui_manager->menu_win, 3, 13 + login_username_pos);
            } else {
                wmove(g_ui_manager->menu_win, 5, 13 + login_password_pos);
            }
            curs_set(1);
            break;
        }
        case APP_STATE_SYNCING: {
            // 시간 동기화 화면
            const char* sync_message = "서버와 시간을 동기화하는 중입니다...";
            int max_y, max_x;
            getmaxyx(g_ui_manager->menu_win, max_y, max_x);
            mvwprintw(g_ui_manager->menu_win, max_y / 2, (max_x - strlen(sync_message)) / 2, "%s", sync_message);
            client_draw_message_win("잠시만 기다려주세요...");
            break;
        }
        case APP_STATE_MAIN_MENU: {
            main_menu.highlight_index = menu_highlight;
            ui_render_menu(g_ui_manager->menu_win, &main_menu);
            client_draw_message_win(main_menu.help_text);
            break;
        }
        case APP_STATE_LOGGED_IN_MENU: {
            logged_in_menu.highlight_index = menu_highlight;
            ui_render_menu(g_ui_manager->menu_win, &logged_in_menu);
            client_draw_message_win(logged_in_menu.help_text);
            break;
        }
        case APP_STATE_DEVICE_LIST: {
            int menu_win_height = getmaxy(g_ui_manager->menu_win);
            client_draw_message_win("[↑↓] 이동  [Enter] 예약/선택  [C] 예약취소  [ESC] 뒤로");
            if (!device_list || device_count == 0) {
                mvwprintw(g_ui_manager->menu_win, 2, 2, "장비 목록을 가져오는 중이거나, 등록된 장비가 없습니다.");
                break;
            }
            time_t current_time = client_get_synced_time();
            ui_draw_device_table(g_ui_manager->menu_win, device_list, device_count, menu_highlight, true, NULL, NULL, current_time, true);
            if (device_count > 0 && menu_highlight < device_count) {
                char help_message[128] = {0};
                device_t* highlighted_device = &device_list[menu_highlight];
                if (highlighted_device->status == DEVICE_RESERVED && strcmp(highlighted_device->reserved_by, client_session.username) == 0) {
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
            break;
        }
        case APP_STATE_RESERVATION_TIME: {
            int menu_win_height = getmaxy(g_ui_manager->menu_win);
            // 장비 목록 테이블 재사용
            client_draw_message_win("[숫자] 시간 입력  [Enter] 예약  [ESC] 취소");
            ui_draw_device_table(g_ui_manager->menu_win, device_list, device_count, menu_highlight, true, NULL, NULL, client_get_synced_time(), true);
            mvwprintw(g_ui_manager->menu_win, LINES - 5, 2, "예약할 시간(초) 입력 (1~86400, ESC:취소): %s", reservation_input_buffer);
            char help_msg[128];
            snprintf(help_msg, sizeof(help_msg), "도움말: 1 ~ 86400 사이의 예약 시간(초)을 입력하고 Enter를 누르세요.");
            mvwprintw(g_ui_manager->menu_win, menu_win_height - 2, 2, "%-s", help_msg);
            curs_set(1);
            break;
        }
        case APP_STATE_EXIT:
        default:
            break;
    }
    box(g_ui_manager->menu_win, 0, 0);
    wrefresh(g_ui_manager->menu_win);
    pthread_mutex_unlock(&g_ui_manager->mutex);
}

static void client_handle_keyboard_input(int ch) {
    switch (current_state) {
        case APP_STATE_LOGIN: {
            // --- client_handle_input_login_input 통합 ---
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
                        active_login_field = LOGIN_FIELD_PASSWORD;
                    } else {
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
                    }
                    break;
                default:
                    if ((ch >= 32 && ch <= 126) && (size_t)(*current_pos) < buffer_size - 1) {
                        current_buffer[(*current_pos)++] = ch;
                        current_buffer[*current_pos] = '\0';
                    }
                    break;
            }
            break;
        }
        case APP_STATE_MAIN_MENU: {
            switch (ch) {
                case KEY_UP:
                    menu_highlight = (menu_highlight == 0) ? 1 : 0;
                    break;
                case KEY_DOWN:
                    menu_highlight = (menu_highlight == 1) ? 0 : 1;
                    break;
                case 10: // Enter 키
                    if (menu_highlight == 0) {
                        current_state = APP_STATE_LOGIN;
                        menu_highlight = 0;
                        active_login_field = LOGIN_FIELD_USERNAME;
                        memset(login_username_buffer, 0, sizeof(login_username_buffer));
                        memset(login_password_buffer, 0, sizeof(login_password_buffer));
                        login_username_pos = 0;
                        login_password_pos = 0;
                    } else {
                        running = false;
                    }
                    break;
                case 27: // ESC
                    running = false;
                    break;
                default:
                    break;
            }
            break;
        }
        case APP_STATE_LOGGED_IN_MENU: {
            switch (ch) {
                case KEY_UP: 
                    menu_highlight = (menu_highlight == 0) ? 1 : 0; 
                    break;
                case KEY_DOWN: 
                    menu_highlight = (menu_highlight == 1) ? 0 : 1; 
                    break;
                case 10: // Enter 키
                    if (menu_highlight == 0) {
                        message_t* msg = message_create(MSG_STATUS_REQUEST, NULL);
                        if (msg) {
                            if (network_send_message(client_session.ssl, msg) < 0) {
                                running = false;
                            }
                            message_destroy(msg);
                        }
                    } else {
                        client_perform_logout();
                    }
                    break;
                case 27: // ESC
                    client_perform_logout();
                    break;
                default:
                    break;
            }
            break;
        }
        case APP_STATE_DEVICE_LIST: {
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
                            message_t* msg = message_create(MSG_CANCEL_REQUEST, NULL);
                            if (msg) {
                                msg->args[0] = strdup(dev->id);
                                if (msg->args[0]) {
                                    msg->arg_count = 1;
                                    if (network_send_message(client_session.ssl, msg) < 0) {
                                        running = false;
                                    }
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
            break;
        }
        case APP_STATE_RESERVATION_TIME: {
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
                                message_t* msg = message_create(MSG_RESERVE_REQUEST, NULL);
                                if (msg) {
                                    msg->args[0] = strdup(dev->id);
                                    msg->args[1] = strdup(time_str);
                                    if (msg->args[0] && msg->args[1]) {
                                        msg->arg_count = 2;
                                        if (network_send_message(client_session.ssl, msg) < 0) {
                                            LOG_ERROR("Client", "예약 요청 전송 실패: 장비=%s, 시간=%d초", dev->id, reservation_time);
                                            running = false;
                                        } else {
                                            LOG_INFO("Client", "예약 요청 전송 성공: 장비=%s, 시간=%d초", dev->id, reservation_time);
                                        }
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
            break;
        }
        default:
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
    message_t* login_msg = message_create(MSG_LOGIN, NULL);
    if (!login_msg) {
        ui_show_error_message("로그인 메시지 생성 실패");
        return;
    }
    
    login_msg->args[0] = strdup(username);
    login_msg->args[1] = strdup(password);
    if (login_msg->args[0] && login_msg->args[1]) {
        login_msg->arg_count = 2;
        if (network_send_message(client_session.ssl, login_msg) < 0) {
            ui_show_error_message("로그인 요청 전송 실패");
            message_destroy(login_msg);
            return;
        }
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