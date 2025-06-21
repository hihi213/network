/**
 * @file ui.c
 * @brief 사용자 인터페이스 모듈 - ncurses 기반 터미널 UI
 * @details 메뉴 시스템, 상태 표시, 에러/성공 메시지 출력 기능을 제공합니다.
 */

// src/ui.c

#include "../include/ui.h"  // UI 관련 헤더 파일 포함
#include "../include/message.h"  // 메시지 관련 헤더 파일 포함

/* --- 전역 변수 --- */
ui_manager_t* g_ui_manager = NULL;  // 전역 UI 매니저 포인터

/* --- 내부(private) 함수 프로토타입 --- */

/* --- 공개(public) 함수 정의 --- */

/**
 * @brief UI 시스템을 초기화합니다.
 * @param mode 초기화할 모드
 * @return 성공 시 0, 실패 시 -1
 */
int ui_init(ui_mode_t mode) {
    setlocale(LC_ALL, "");
    g_ui_manager = (ui_manager_t*)malloc(sizeof(ui_manager_t));
    if (!g_ui_manager) return -1;
    memset(g_ui_manager, 0, sizeof(ui_manager_t));
    g_ui_manager->mode = mode;
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_WHITE, COLOR_BLUE);
        init_pair(2, COLOR_YELLOW, COLOR_BLACK);
        init_pair(3, COLOR_RED, COLOR_BLACK);
        init_pair(4, COLOR_GREEN, COLOR_BLACK);
        init_pair(5, COLOR_CYAN, COLOR_BLACK);
    }
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    g_ui_manager->message_win = newwin(1, max_x, 0, 0);
    g_ui_manager->menu_win = newwin(max_y - 4, max_x, 1, 0);
    g_ui_manager->status_win = newwin(3, max_x, max_y - 3, 0);
    box(g_ui_manager->menu_win, 0, 0);
    box(g_ui_manager->status_win, 0, 0);
    box(g_ui_manager->message_win, 0, 0);
    scrollok(g_ui_manager->menu_win, TRUE);
    keypad(g_ui_manager->menu_win, TRUE);
    pthread_mutex_init(&g_ui_manager->mutex, NULL);
    refresh();
    wrefresh(g_ui_manager->menu_win);
    wrefresh(g_ui_manager->status_win);
    wrefresh(g_ui_manager->message_win);
    return 0;
}

/**
 * @brief UI 시스템의 메모리를 정리합니다.
 */
void ui_cleanup(void) {
    if (g_ui_manager) {  // 전역 UI 매니저가 존재하는 경우
        // ui_cleanup_manager(g_ui_manager);  // UI 매니저 정리 (삭제)
        free(g_ui_manager);
        g_ui_manager = NULL;  // 포인터를 NULL로 설정
        // LOG_INFO("UI", "UI 시스템 정리 완료");  // 정보 로그 출력
    }
}

/**
 * @brief 메시지를 화면에 표시합니다.
 * @param prefix 메시지 앞에 붙일 접두사
 * @param message 표시할 메시지
 * @param color_pair 메시지의 색상 쌍
 */
void ui_show_message(const char* prefix, const char* message, int color_pair) {
    if (!g_ui_manager || !message) return;
    pthread_mutex_lock(&g_ui_manager->mutex);
    werase(g_ui_manager->status_win);
    box(g_ui_manager->status_win, 0, 0);
    wattron(g_ui_manager->status_win, COLOR_PAIR(color_pair));
    mvwprintw(g_ui_manager->status_win, 1, 2, "%s: %s", prefix, message);
    wattroff(g_ui_manager->status_win, COLOR_PAIR(color_pair));
    wrefresh(g_ui_manager->status_win);
    pthread_mutex_unlock(&g_ui_manager->mutex);
}

/**
 * @brief 에러 메시지를 화면에 표시합니다.
 * @param message 표시할 에러 메시지
 */
void ui_show_error_message(const char* message) {
    ui_show_message("ERROR", message, 3);
}

/**
 * @brief 성공 메시지를 화면에 표시합니다.
 * @param message 표시할 성공 메시지
 */
void ui_show_success_message(const char* message) {
    ui_show_message("SUCCESS", message, 4);
}

/**
 * @brief 모든 윈도우를 새로고침합니다.
 */
void ui_refresh_all_windows(void) {
    if (!g_ui_manager) return;  // UI 매니저가 NULL이면 함수 종료
    pthread_mutex_lock(&g_ui_manager->mutex);  // UI 매니저 뮤텍스 잠금
    if (g_ui_manager->main_win) wnoutrefresh(g_ui_manager->main_win);  // 메인 윈도우 새로고침
    if (g_ui_manager->status_win) wnoutrefresh(g_ui_manager->status_win);  // 상태 윈도우 새로고침
    if (g_ui_manager->menu_win) wnoutrefresh(g_ui_manager->menu_win);  // 메뉴 윈도우 새로고침
    doupdate();  // 화면 업데이트
    pthread_mutex_unlock(&g_ui_manager->mutex);  // UI 매니저 뮤텍스 해제
}

void ui_handle_resize(void) {
    if (!g_ui_manager) return;
    delwin(g_ui_manager->menu_win);
    delwin(g_ui_manager->status_win);
    delwin(g_ui_manager->message_win);
    endwin();
    refresh();
    clear();
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    g_ui_manager->message_win = newwin(1, max_x, 0, 0);
    g_ui_manager->menu_win = newwin(max_y - 4, max_x, 1, 0);
    g_ui_manager->status_win = newwin(3, max_x, max_y - 3, 0);
    box(g_ui_manager->menu_win, 0, 0);
    box(g_ui_manager->status_win, 0, 0);
    box(g_ui_manager->message_win, 0, 0);
    scrollok(g_ui_manager->menu_win, TRUE);
    keypad(g_ui_manager->menu_win, TRUE);
    refresh();
    wrefresh(g_ui_manager->menu_win);
    wrefresh(g_ui_manager->status_win);
    wrefresh(g_ui_manager->message_win);
}

// 한글/영문 혼용 문자열의 실제 표시 폭 계산
int get_display_width(const char* str) {
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
void print_fixed_width(WINDOW* win, int y, int x, const char* str, int width) {
    mvwprintw(win, y, x, "%s", str);
    int disp = get_display_width(str);
    for (int i = disp; i < width; ++i) {
        mvwaddch(win, y, x + i, ' ');
    }
}

/**
 * @brief 장비 목록 테이블을 그리는 공통 함수
 * @param win 출력할 윈도우
 * @param devices 장비 배열
 * @param count 장비 개수
 * @param highlight_row 하이라이트할 행 (-1이면 없음)
 * @param show_remaining_time 남은 시간 표시 여부
 * @param reservation_manager 예약 매니저 (서버용, 클라이언트는 NULL)
 * @param resource_manager 리소스 매니저 (서버용, 클라이언트는 NULL)
 * @param current_time 현재 시간 (클라이언트용)
 */
void ui_draw_device_table(WINDOW* win, device_t* devices, int count, int highlight_row, 
                         bool show_remaining_time, void* reservation_manager, void* resource_manager, 
                         time_t current_time) {
    if (!win || !devices) return;
    
    int win_height, win_width;
    getmaxyx(win, win_height, win_width);
    
    // 컬럼 위치/폭 설정
    int col_w[6] = {10, 27, 15, 14, 8, 8};
    int col_x[6];
    col_x[0] = 2;
    for (int i = 1; i < 6; ++i) {
        col_x[i] = col_x[i-1] + col_w[i-1] + 1;
    }
    
    // 헤더
    wattron(win, A_BOLD);
    print_fixed_width(win, 1, col_x[0], "ID", col_w[0]);
    print_fixed_width(win, 1, col_x[1], "이름", col_w[1]);
    print_fixed_width(win, 1, col_x[2], "타입", col_w[2]);
    print_fixed_width(win, 1, col_x[3], "상태", col_w[3]);
    print_fixed_width(win, 1, col_x[4], "예약자", col_w[4]);
    if (show_remaining_time) {
        print_fixed_width(win, 1, col_x[5], "남은시간", col_w[5]);
    }
    wattroff(win, A_BOLD);
    
    // 구분선
    for (int i = 0; i < 6; ++i) {
        mvwaddch(win, 1, col_x[i] - 2, '|');
    }
    
    // 장비 목록
    int max_rows = win_height - 4;
    for (int i = 0; i < count && i < max_rows; i++) {
        const device_t* device = &devices[i];
        char reservation_info[32] = "-";
        char remain_str[16] = "-";
        
        if (device->status == DEVICE_RESERVED) {
            if (reservation_manager && resource_manager) {
                // 서버 모드: reservation_manager를 통해 예약 정보 조회
                reservation_t* res = reservation_get_active_for_device(
                    (reservation_manager_t*)reservation_manager, 
                    (resource_manager_t*)resource_manager, 
                    device->id);
                if (res) {
                    time_t now = time(NULL);
                    long remaining_sec = (res->end_time > now) ? (res->end_time - now) : 0;
                    snprintf(reservation_info, sizeof(reservation_info), "%s", res->username);
                    snprintf(remain_str, sizeof(remain_str), "%lds", remaining_sec);
                }
            } else {
                // 클라이언트 모드: 로컬 데이터 사용
                strncpy(reservation_info, device->reserved_by, sizeof(reservation_info) - 1);
                reservation_info[sizeof(reservation_info) - 1] = '\0';
                
                if (current_time > 0 && device->reservation_end_time > 0) {
                    long remaining_sec = (device->reservation_end_time > current_time) ? 
                                        (device->reservation_end_time - current_time) : 0;
                    snprintf(remain_str, sizeof(remain_str), "%lds", remaining_sec);
                }
            }
        }
        
        const char* status_str = message_get_device_status_string(device->status);
        
        // 상태별 색상 강조 (서버 모드에서만)
        if (reservation_manager) {
            if (device->status == DEVICE_RESERVED) wattron(win, COLOR_PAIR(2));
            else if (device->status == DEVICE_MAINTENANCE) wattron(win, COLOR_PAIR(3));
            else if (device->status == DEVICE_AVAILABLE) wattron(win, COLOR_PAIR(4));
        }
        
        // 하이라이트 처리 (클라이언트 모드에서만)
        if (highlight_row == i) {
            wattron(win, A_REVERSE);
        }
        
        print_fixed_width(win, i + 2, col_x[0], device->id, col_w[0]);
        print_fixed_width(win, i + 2, col_x[1], device->name, col_w[1]);
        print_fixed_width(win, i + 2, col_x[2], device->type, col_w[2]);
        print_fixed_width(win, i + 2, col_x[3], status_str, col_w[3]);
        print_fixed_width(win, i + 2, col_x[4], reservation_info, col_w[4]);
        if (show_remaining_time) {
            print_fixed_width(win, i + 2, col_x[5], remain_str, col_w[5]);
        }
        
        // 색상 및 하이라이트 해제
        if (reservation_manager) {
            wattroff(win, COLOR_PAIR(2));
            wattroff(win, COLOR_PAIR(3));
            wattroff(win, COLOR_PAIR(4));
        }
        if (highlight_row == i) {
            wattroff(win, A_REVERSE);
        }
    }
}