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
static void ui_set_error_message(ui_manager_t* manager, const char* message);  // 에러 메시지 설정 함수
static void ui_display_message_on_window(WINDOW* win, int color_pair, const char* prefix, const char* message);  // 메시지를 윈도우에 표시하는 함수

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
    g_ui_manager->menu_win = newwin(max_y - 3, max_x, 1, 0);
    g_ui_manager->status_win = newwin(2, max_x, max_y - 2, 0);
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
        ui_cleanup_manager(g_ui_manager);  // UI 매니저 정리
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

/**
 * @brief UI 매니저를 초기화합니다.
 * @return 성공 시 초기화된 UIManager 포인터, 실패 시 NULL
 */
ui_manager_t* ui_init_manager(void) {
    ui_manager_t* manager = (ui_manager_t*)malloc(sizeof(ui_manager_t));
    if (!manager) {
        utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "UI", "UI 매니저 메모리 할당 실패");
        return NULL;
    }

    // ncurses 초기화
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    // 색상 지원 확인 및 초기화
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_WHITE, COLOR_BLUE);   // 메뉴 선택
        init_pair(2, COLOR_YELLOW, COLOR_BLACK); // 경고
        init_pair(3, COLOR_RED, COLOR_BLACK);    // 에러
        init_pair(4, COLOR_GREEN, COLOR_BLACK);  // 성공
        init_pair(5, COLOR_CYAN, COLOR_BLACK);   // 정보
    }

    // 터미널 크기 확인
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    if (max_y < 24 || max_x < 80) {
        utils_report_error(ERROR_UI_TERMINAL_TOO_SMALL, "UI", "터미널 크기가 너무 작습니다 (최소 80x24 필요).");
        endwin();
        free(manager);
        return NULL;
    }

    // 윈도우 생성
    manager->menu_win = newwin(max_y - 3, max_x, 1, 0);
    manager->status_win = newwin(2, max_x, max_y - 2, 0);
    manager->message_win = newwin(1, max_x, 0, 0);

    if (!manager->menu_win || !manager->status_win || !manager->message_win) {
        utils_report_error(ERROR_UI_INIT_FAILED, "UI", "윈도우 생성 실패");
        if (manager->menu_win) delwin(manager->menu_win);
        if (manager->status_win) delwin(manager->status_win);
        if (manager->message_win) delwin(manager->message_win);
        endwin();
        free(manager);
        return NULL;
    }

    // 윈도우 설정
    box(manager->menu_win, 0, 0);
    box(manager->status_win, 0, 0);
    box(manager->message_win, 0, 0);

    // 스크롤 활성화
    scrollok(manager->menu_win, TRUE);
    keypad(manager->menu_win, TRUE);

    // 초기 상태 설정
    manager->current_menu = MAIN_MENU;
    manager->selected_item = 0;
    manager->error_message[0] = '\0';
    manager->success_message[0] = '\0';

    refresh();
    wrefresh(manager->menu_win);
    wrefresh(manager->status_win);
    wrefresh(manager->message_win);

    return manager;
}

/**
 * @brief UI 매니저의 메모리를 정리합니다.
 * @param manager 정리할 UIManager 포인터
 */
void ui_cleanup_manager(ui_manager_t* manager) {
    if (!manager) return;  // 매니저가 NULL이면 함수 종료
    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금
    if (manager->status_win) delwin(manager->status_win);  // 상태 윈도우 삭제
    if (manager->menu_win) delwin(manager->menu_win);  // 메뉴 윈도우 삭제
    endwin(); // ncurses 종료
    pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
    pthread_mutex_destroy(&manager->mutex);  // 뮤텍스 정리
    free(manager);  // 매니저 메모리 해제
}

/**
 * @brief 상태 메시지를 설정합니다.
 * @param manager UI 매니저 포인터
 * @param message 표시할 상태 메시지
 */
void ui_set_status_message(ui_manager_t* manager, const char* message) {
    (void)manager;
    ui_show_message("STATUS", message, 4);
}

/**
 * @brief 에러 메시지를 설정합니다.
 * @param manager UI 매니저 포인터
 * @param message 표시할 에러 메시지
 */
void ui_set_error_message(ui_manager_t* manager, const char* message) {
    (void)manager;
    ui_show_message("ERROR", message, 3);
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
    g_ui_manager->menu_win = newwin(max_y - 3, max_x, 1, 0);
    g_ui_manager->status_win = newwin(2, max_x, max_y - 2, 0);
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

void ui_show_status(const char* msg) {
    ui_show_message("STATUS", msg, 4);
}

void ui_show_error(const char* msg) {
    ui_show_message("ERROR", msg, 3);
}

// 내부 헬퍼 함수: 메시지를 윈도우에 표시
static void ui_display_message_on_window(WINDOW* win, int color_pair, const char* prefix, const char* message) {
    if (!win) return;
    werase(win);
    box(win, 0, 0);
    wattron(win, COLOR_PAIR(color_pair));
    mvwprintw(win, 1, 2, "%s: %s", prefix, message);
    wattroff(win, COLOR_PAIR(color_pair));
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