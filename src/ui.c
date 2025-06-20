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