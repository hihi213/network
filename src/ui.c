/**
 * @file ui.c
 * @brief 사용자 인터페이스 모듈 - ncurses 기반 터미널 UI
 * 
 * @details
 * 이 모듈은 터미널 기반의 실시간 UI를 제공합니다:
 * 
 * 1. **윈도우 레이아웃**: 메시지, 메뉴, 상태창 분리 및 동적 리사이즈 지원
 * 2. **동시성 제어**: UI 갱신 시 뮤텍스 사용으로 스레드 안전성 보장
 * 3. **장비 테이블 렌더링**: 한글/영문 혼용 폭 계산, 컬러 강조, 남은 시간 표시
 * 4. **메뉴 시스템**: 동적 메뉴 렌더링 및 도움말 표시
 * 
 * @note 모든 UI 함수는 g_ui_manager 전역 객체를 통해 상태를 관리하며,
 *       멀티스레드 환경에서 안전하게 동작합니다.
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
 * @param use_color 색상 적용 여부
 */
void ui_draw_device_table(WINDOW* win, device_t* devices, int count, int highlight_row, 
                         bool show_remaining_time, void* reservation_manager, void* resource_manager, 
                         time_t current_time, bool use_color) {
    if (!win || !devices) return;
    
    int win_height, win_width;
    getmaxyx(win, win_height, win_width);
    (void)win_width; // 사용되지 않는 변수 경고를 막기 위함
    
    // 컬럼 위치/폭 설정 (5개 컬럼으로 변경)
    int col_w[5] = {10, 27, 15, 14, 20}; // 예약자 컬럼 폭을 늘림
    int col_x[5];
    col_x[0] = 2;
    for (int i = 1; i < 5; ++i) {
        col_x[i] = col_x[i-1] + col_w[i-1] + 1;
    }
    
    // 헤더
    wattron(win, A_BOLD);
    print_fixed_width(win, 1, col_x[0], "ID", col_w[0]);
    print_fixed_width(win, 1, col_x[1], "이름", col_w[1]);
    print_fixed_width(win, 1, col_x[2], "타입", col_w[2]);
    print_fixed_width(win, 1, col_x[3], "상태", col_w[3]);
    print_fixed_width(win, 1, col_x[4], "예약정보", col_w[4]);
    wattroff(win, A_BOLD);
    
    // 구분선
    for (int i = 0; i < 5; ++i) {
        mvwaddch(win, 1, col_x[i] - 2, '|');
    }
    
    // 장비 목록
    int max_rows = win_height - 4;
    for (int i = 0; i < count && i < max_rows; i++) {
        const device_t* device = &devices[i];
        
        // [수정 시작] 화면에 표시할 내용을 담을 임시 변수 선언
        char display_reservation_info[64] = "-";
        const char* display_status_str = message_get_device_status_string(device->status);
        int status_color_pair = 0; // 색상 페어 임시 변수
        
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
                    
                    // [주석처리] 서버 UI 시간 계산 로그
                    // LOG_INFO("ServerUI", "장비[%s] 예약 정보 계산: 사용자=%s, 종료시간=%ld, 현재시간=%ld, 남은시간=%ld초", 
                    //          device->id, res->username, res->end_time, now, remaining_sec);
                    
                    if (show_remaining_time) {
                        snprintf(display_reservation_info, sizeof(display_reservation_info), "%s(%lds)", res->username, remaining_sec);
                    } else {
                        snprintf(display_reservation_info, sizeof(display_reservation_info), "%s", res->username);
                    }
                    status_color_pair = 2; // 'reserved'에 해당하는 노란색
                    
                    // [주석처리] 남은 시간이 0초 이하인 경우 로그
                    // if (remaining_sec <= 0) {
                    //     LOG_WARNING("ServerUI", "장비[%s] 예약 만료됨: 사용자=%s, 종료시간=%ld, 현재시간=%ld, 시간차=%ld초", 
                    //                device->id, res->username, res->end_time, now, now - res->end_time);
                    // }
                } else {
                    LOG_WARNING("ServerUI", "장비[%s] 예약 상태이지만 예약 정보를 찾을 수 없음", device->id);
                    status_color_pair = 2; // 'reserved'에 해당하는 노란색
                }
            } else {
                // [개선된 클라이언트 모드 로직]
                if (strlen(device->reserved_by) > 0 && device->reservation_end_time > 0) { // 예약자 정보와 종료 시간이 유효한 경우
                    // [핵심] 현재 시간과 예약 종료 시간을 직접 비교하여 상태를 결정
                    if (current_time >device->reservation_end_time) {
                        // 시간이 만료된 경우, 'available'로 표시
                        display_status_str = "available";
                        strcpy(display_reservation_info, "-");
                        status_color_pair = 4; // 'available'에 해당하는 녹색
                    } else {
                        // 아직 시간이 남은 경우, 남은 시간 표시
                        long remaining_sec = device->reservation_end_time - current_time;
                        snprintf(display_reservation_info, sizeof(display_reservation_info), "%s(%lds)", device->reserved_by, remaining_sec);
                        status_color_pair = 2; // 'reserved'에 해당하는 노란색
                    }
                } else {
                    // 예약 정보가 불완전하지만 상태가 'RESERVED'인 경우
                    status_color_pair = 2; // 'reserved'에 해당하는 노란색
                }
            }
        } else if (device->status == DEVICE_AVAILABLE) {
            status_color_pair = 4; // 녹색
        } else { // DEVICE_MAINTENANCE
            status_color_pair = 3; // 빨간색
        }
        
        // 색상 강조 (use_color가 true일 때만)
        if (use_color && status_color_pair > 0) {
            wattron(win, COLOR_PAIR(status_color_pair));
        }
        
        if (highlight_row == i) {
            wattron(win, A_REVERSE);
        }
        
        // 임시 변수를 사용하여 화면에 출력 (원본 데이터는 변경하지 않음)
        print_fixed_width(win, i + 2, col_x[0], device->id, col_w[0]);
        print_fixed_width(win, i + 2, col_x[1], device->name, col_w[1]);
        print_fixed_width(win, i + 2, col_x[2], device->type, col_w[2]);
        print_fixed_width(win, i + 2, col_x[3], display_status_str, col_w[3]);
        print_fixed_width(win, i + 2, col_x[4], display_reservation_info, col_w[4]);

        if (use_color && status_color_pair > 0) {
            wattroff(win, COLOR_PAIR(status_color_pair));
        }
        
        if (highlight_row == i) {
            wattroff(win, A_REVERSE);
        }
    }
}

/**
 * @brief 메뉴를 렌더링하는 함수
 * @param win 출력할 윈도우
 * @param menu 렌더링할 메뉴 구조체
 */
void ui_render_menu(WINDOW* win, const ui_menu_t* menu) {
    if (!win || !menu || !menu->items) return;
    
    werase(win);
    box(win, 0, 0);

    int win_height, win_width;
    getmaxyx(win, win_height, win_width);
    (void)win_height; // 사용되지 않는 변수 경고를 막기 위함
    
    // 메뉴 제목 출력
    if (menu->title) {
        wattron(win, A_BOLD);
        mvwprintw(win, 1, 2, "%s", menu->title);
        wattroff(win, A_BOLD);
    }
    
    // 메뉴 아이템들 출력
    int start_y = menu->title ? 3 : 2;
    for (int i = 0; i < menu->item_count && (start_y + i) < win_height - 2; i++) {
        const ui_menu_item_t* item = &menu->items[i];
        
        // 비활성화된 아이템은 회색으로 표시
        if (!item->enabled) {
            wattron(win, A_DIM);
        }

        // 하이라이트된 아이템은 반전 표시
        if (i == menu->highlight_index) {
            wattron(win, A_REVERSE);
        }
        
        // 메뉴 아이템 출력
        mvwprintw(win, start_y + i, 2, " > %s", item->text);
        
        // 속성 해제
        if (i == menu->highlight_index) {
            wattroff(win, A_REVERSE);
        }
        if (!item->enabled) {
            wattroff(win, A_DIM);
        }
    }
    
    // 도움말 텍스트 출력
    if (menu->help_text) {
        mvwprintw(win, win_height - 2, 2, "%s", menu->help_text);
    }
}