/**
 * @file ui.c
 * @brief 사용자 인터페이스 모듈 - ncurses 기반 터미널 UI
 * @details 메뉴 시스템, 상태 표시, 에러/성공 메시지 출력 기능을 제공합니다.
 */

// src/ui.c

#include "../include/ui.h"
#include "../include/message.h"

/* --- 전역 변수 --- */
UIManager* global_ui_manager = NULL;

/* --- 내부(private) 함수 프로토타입 --- */
static void set_error_message(UIManager* manager, const char* message);

/* --- 공개(public) 함수 정의 --- */

/**
 * @brief UI 시스템을 초기화합니다.
 * @return 성공 시 0, 실패 시 -1
 */
int init_ui(void) {
    setlocale(LC_ALL, ""); // 로케일 설정 (한글 지원)
    global_ui_manager = init_ui_manager();
    if (!global_ui_manager) {
        fprintf(stderr, "UI 매니저 초기화 실패\n");
        return -1;
    }
    LOG_INFO("UI", "UI 시스템 초기화 성공");
    return 0;
}

/**
 * @brief UI 시스템의 메모리를 정리합니다.
 */
void cleanup_ui(void) {
    if (global_ui_manager) {
        cleanup_ui_manager(global_ui_manager);
        global_ui_manager = NULL;
        LOG_INFO("UI", "UI 시스템 정리 완료");
    }
}

/**
 * @brief 메뉴를 생성하는 함수 (새로운 구조에서는 더 이상 사용되지 않음)
 * @param manager UI 매니저 포인터
 * @param title 메뉴 제목
 * @param items 메뉴 항목 배열
 * @param count 메뉴 항목 개수
 * @return 항상 -1 (사용되지 않음)
 */
int create_menu(UIManager* manager, const char* title, const char** items, int count) {
    return -1;
}

/**
 * @brief 에러 메시지를 화면에 표시합니다.
 * @param message 표시할 에러 메시지
 */
void show_error_message(const char* message) {
    if (!global_ui_manager || !message) return;
    set_error_message(global_ui_manager, message);
    refresh_all_windows();
}

/**
 * @brief 성공 메시지를 화면에 표시합니다.
 * @param message 표시할 성공 메시지
 */
void show_success_message(const char* message) {
    if (!global_ui_manager || !message) return;
    set_status_message(global_ui_manager, message);
    refresh_all_windows();
}

/**
 * @brief 서버 상태 정보를 화면에 업데이트합니다.
 * @param session_count 활성 세션 수
 * @param port 서버 포트 번호
 */
void update_server_status(int session_count, int port) {
    if (!global_ui_manager) return;
    char status_msg[MAX_MESSAGE_LENGTH];
    snprintf(status_msg, sizeof(status_msg), "Server Running on Port: %d | Active Sessions: %d", port, session_count);
    set_status_message(global_ui_manager, status_msg);
}

/**
 * @brief 서버의 장비 목록을 화면에 업데이트합니다.
 * @param devices 장비 배열
 * @param count 장비 개수
 * @param resource_manager 리소스 매니저 (예약 정보 조회용)
 * @param reservation_manager 예약 매니저 (예약 정보 조회용)
 */
void update_server_devices(const Device* devices, int count, ResourceManager* resource_manager, ReservationManager* reservation_manager){
    if (!global_ui_manager || !devices) return;
    pthread_mutex_lock(&global_ui_manager->mutex);
    WINDOW* win = global_ui_manager->menu_win;
    werase(win);
    box(win, 0, 0);
    
    // 헤더 출력
    wattron(win, A_BOLD);
    mvwprintw(win, 1, 2, "%-10s | %-25s | %-15s | %s", "ID", "Name", "Type", "Status");
    wattroff(win, A_BOLD);
    mvwaddstr(win, 2, 2, "------------------------------------------------------------------");
    
    // 장비 목록 출력
    for (int i = 0; i < count; i++) {
        if (i + 3 >= LINES - 6) break; // 화면 크기 제한
        char status_display_str[128];
        const char* base_status_str = get_device_status_string(devices[i].status);
        
        // 예약된 장비의 경우 남은 시간 표시
        if (devices[i].status == DEVICE_RESERVED && reservation_manager) {
             Reservation* res = get_active_reservation_for_device(reservation_manager, resource_manager, devices[i].id);
            if (res) {
                time_t now = time(NULL);
                long remaining_sec = (res->end_time > now) ? (res->end_time - now) : 0;
                snprintf(status_display_str, sizeof(status_display_str), "%s (%lds left)", base_status_str, remaining_sec);
            } else {
                strncpy(status_display_str, base_status_str, sizeof(status_display_str) - 1);
            }
        } else {
            strncpy(status_display_str, base_status_str, sizeof(status_display_str) - 1);
        }
        status_display_str[sizeof(status_display_str) - 1] = '\0';
        
        mvwprintw(win, i + 3, 2, "%-10s | %-25s | %-15s | %s",
                  devices[i].id, devices[i].name, devices[i].type, status_display_str);
    }
    pthread_mutex_unlock(&global_ui_manager->mutex);
}

/**
 * @brief 모든 윈도우를 새로고침합니다.
 */
void refresh_all_windows(void) {
    if (!global_ui_manager) return;
    pthread_mutex_lock(&global_ui_manager->mutex);
    if (global_ui_manager->main_win) wnoutrefresh(global_ui_manager->main_win);
    if (global_ui_manager->status_win) wnoutrefresh(global_ui_manager->status_win);
    if (global_ui_manager->menu_win) wnoutrefresh(global_ui_manager->menu_win);
    doupdate();
    pthread_mutex_unlock(&global_ui_manager->mutex);
}

/**
 * @brief UI 매니저를 초기화합니다.
 * @return 성공 시 초기화된 UIManager 포인터, 실패 시 NULL
 */
UIManager* init_ui_manager(void) {
    UIManager* manager = (UIManager*)malloc(sizeof(UIManager));
    if (!manager) return NULL;
    memset(manager, 0, sizeof(UIManager));
    
    // 뮤텍스 초기화
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) { 
        free(manager); 
        return NULL; 
    }
    
    // ncurses 초기화
    if (initscr() == NULL) { 
        pthread_mutex_destroy(&manager->mutex); 
        free(manager); 
        return NULL; 
    }
    
    // ncurses 설정
    start_color();
    cbreak();      // 라인 버퍼링 비활성화
    noecho();      // 입력 에코 비활성화
    curs_set(0);   // 커서 숨기기
    
    // 터미널 크기 확인
    int term_h, term_w;
    getmaxyx(stdscr, term_h, term_w);
    if (term_h < 24 || term_w < 80) {
        endwin();
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        fprintf(stderr, "터미널 크기가 너무 작습니다 (최소 80x24 필요).\n");
        return NULL;
    }

    // 윈도우 생성
    manager->main_win = stdscr;
    manager->menu_win = newwin(term_h - 4, term_w, 1, 0);
    manager->status_win = newwin(3, term_w, term_h - 3, 0);
    
    // ================================================================
    // ▼ [핵심 수정] 아래 두 줄로 키보드 입력을 올바르게 설정합니다. ▼
    keypad(manager->menu_win, TRUE);     // 방향키, 기능키 입력을 가능하게 함
    nodelay(manager->menu_win, TRUE);    // 입력 대기(blocking)를 완전히 비활성화
    // ▲ [핵심 수정] 위 두 줄로 키보드 입력을 올바르게 설정합니다. ▲
    // ================================================================

    // 윈도우 테두리 그리기
    box(manager->menu_win, 0, 0);
    box(manager->status_win, 0, 0);

    // 색상 쌍 초기화
    init_pair(1, COLOR_WHITE, COLOR_BLUE);   // 메뉴 하이라이트
    init_pair(2, COLOR_WHITE, COLOR_BLACK);  // 기본 텍스트
    init_pair(3, COLOR_BLACK, COLOR_WHITE);  // 반전 텍스트
    init_pair(4, COLOR_WHITE, COLOR_GREEN);  // 성공 메시지
    init_pair(5, COLOR_WHITE, COLOR_RED);    // 에러 메시지

    return manager;
}

/**
 * @brief UI 매니저의 메모리를 정리합니다.
 * @param manager 정리할 UIManager 포인터
 */
void cleanup_ui_manager(UIManager* manager) {
    if (!manager) return;
    pthread_mutex_lock(&manager->mutex);
    if (manager->status_win) delwin(manager->status_win);
    if (manager->menu_win) delwin(manager->menu_win);
    endwin(); // ncurses 종료
    pthread_mutex_unlock(&manager->mutex);
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/**
 * @brief 상태 메시지를 설정합니다.
 * @param manager UI 매니저 포인터
 * @param message 표시할 상태 메시지
 */
void set_status_message(UIManager* manager, const char* message) {
    if (!manager || !message) return;
    werase(manager->status_win);
    box(manager->status_win, 0, 0);
    wattron(manager->status_win, COLOR_PAIR(4)); // 녹색 배경
    mvwprintw(manager->status_win, 1, 2, "STATUS: %s", message);
    wattroff(manager->status_win, COLOR_PAIR(4));
}

/**
 * @brief 에러 메시지를 설정합니다.
 * @param manager UI 매니저 포인터
 * @param message 표시할 에러 메시지
 */
void set_error_message(UIManager* manager, const char* message) {
    if (!manager || !message) return;
    werase(manager->status_win);
    box(manager->status_win, 0, 0);
    wattron(manager->status_win, COLOR_PAIR(5)); // 빨간색 배경
    mvwprintw(manager->status_win, 1, 2, "ERROR: %s", message);
    wattroff(manager->status_win, COLOR_PAIR(5));
}