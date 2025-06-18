#include "../include/ui.h"
#include "../include/message.h"

/* --- 전역 변수 --- */
UIManager* global_ui_manager = NULL;

/* --- 내부(private) 함수 프로토타입 --- */
static void set_error_message(UIManager* manager, const char* message);

/* --- 공개(public) 함수 정의 --- */

/**
 * @brief UI 시스템을 초기화합니다. 프로그램 시작 시 한 번만 호출됩니다.
 */
int init_ui(void) {
    setlocale(LC_ALL, ""); // 한글 등 다국어 문자 지원
    global_ui_manager = init_ui_manager();
    if (!global_ui_manager) {
        fprintf(stderr, "UI 매니저 초기화 실패\n");
        return -1;
    }
    LOG_INFO("UI", "UI 시스템 초기화 성공");
    return 0;
}

/**
 * @brief UI 시스템을 종료하고 자원을 정리합니다. 프로그램 종료 시 한 번만 호출됩니다.
 */
void cleanup_ui(void) {
    if (global_ui_manager) {
        cleanup_ui_manager(global_ui_manager);
        global_ui_manager = NULL;
        LOG_INFO("UI", "UI 시스템 정리 완료");
    }
}

/**
 * @brief [핵심] 모든 상호작용 메뉴를 생성하고 사용자 선택을 받아 인덱스를 반환하는 유일한 함수.
 * @param manager UI 관리자 포인터.
 * @param title 메뉴 창의 제목.
 * @param items 표시할 메뉴 항목들의 문자열 배열.
 * @param count 항목의 개수.
 * @return 사용자가 선택한 항목의 인덱스 (0부터 시작). ESC나 'q'로 취소 시 -1을 반환.
 */
int create_menu(UIManager* manager, const char* title, const char** items, int count) {
    if (!manager || !items || count <= 0) {
        LOG_ERROR("UI", "create_menu: 잘못된 파라미터");
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);

    ITEM** menu_items = (ITEM**)calloc(count + 1, sizeof(ITEM*));
    for (int i = 0; i < count; i++) {
        menu_items[i] = new_item(items[i], "");
    }
    menu_items[count] = NULL;

    MENU* menu = new_menu(menu_items);
    
    werase(manager->menu_win);
    box(manager->menu_win, 0, 0);
    if (title) {
        mvwprintw(manager->menu_win, 0, 2, " %s ", title);
    }
    
    set_menu_win(menu, manager->menu_win);
    WINDOW* menu_sub = derwin(manager->menu_win, LINES - 8, COLS - 4, 1, 2);
    set_menu_sub(menu, menu_sub);
    set_menu_mark(menu, " > ");
    post_menu(menu);
    wrefresh(manager->menu_win);

    int selected_index = -1;
    int ch;
    keypad(manager->menu_win, TRUE);
    while ((ch = wgetch(manager->menu_win))) {
        switch (ch) {
            case KEY_DOWN: menu_driver(menu, REQ_DOWN_ITEM); break;
            case KEY_UP: menu_driver(menu, REQ_UP_ITEM); break;
            case 27: case 'q': case 'Q':
                selected_index = -1;
                goto end_menu_loop;
            case 10: case KEY_ENTER:
                selected_index = item_index(current_item(menu));
                goto end_menu_loop;
        }
        wrefresh(manager->menu_win);
    }

end_menu_loop:
    unpost_menu(menu);
    free_menu(menu);
    for (int i = 0; i < count; i++) free_item(menu_items[i]);
    free(menu_items);
    delwin(menu_sub);
    werase(manager->menu_win);
    box(manager->menu_win, 0, 0);
    wrefresh(manager->menu_win);

    pthread_mutex_unlock(&manager->mutex);
    return selected_index;
}

/**
 * @brief 에러 메시지를 상태바에 표시하는 공개 함수.
 */
void show_error_message(const char* message) {
    if (!global_ui_manager || !message) return;
    set_error_message(global_ui_manager, message); // [수정] 재귀 호출 버그 수정
    refresh_all_windows(); // 즉시 화면에 반영
}

/**
 * @brief 성공/일반 메시지를 상태바에 표시하는 공개 함수.
 */
void show_success_message(const char* message) {
    if (!global_ui_manager || !message) return;
    set_status_message(global_ui_manager, message);
    refresh_all_windows(); // 즉시 화면에 반영
}

/**
 * @brief 서버의 상태(세션 수, 포트)를 UI에 업데이트합니다.
 */
void update_server_status(int session_count, int port) {
    if (!global_ui_manager) return;
    char status_msg[MAX_MESSAGE_LENGTH];
    snprintf(status_msg, sizeof(status_msg), "Server Running on Port: %d | Active Sessions: %d", port, session_count);
    set_status_message(global_ui_manager, status_msg);
}

/**
 * @brief 서버의 장비 목록을 UI에 '비상호작용' 형태로 표시합니다.
 */
void update_server_devices(const Device* devices, int count, ResourceManager* resource_manager, ReservationManager* reservation_manager){
    if (!global_ui_manager || !devices) return;

    pthread_mutex_lock(&global_ui_manager->mutex);
    WINDOW* win = global_ui_manager->menu_win;
    werase(win);
    box(win, 0, 0);
    wattron(win, A_BOLD);
    mvwprintw(win, 1, 2, "%-10s | %-25s | %-15s | %s", "ID", "Name", "Type", "Status");
    wattroff(win, A_BOLD);
    mvwaddstr(win, 2, 2, "------------------------------------------------------------------");
    
    for (int i = 0; i < count; i++) {
        if (i + 3 >= LINES - 6) break;
        
        char status_display_str[128];
        const char* base_status_str = get_device_status_string(devices[i].status);

        // [추가] 예약된 장비의 경우 남은 시간 계산
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
 * @brief 모든 UI 창을 화면에 새로고침합니다.
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


/* --- 내부(private) 함수 정의 --- */

/**
 * @brief UI 관리자와 ncurses 관련 자원을 초기화합니다.
 */
UIManager* init_ui_manager(void) {
    UIManager* manager = (UIManager*)malloc(sizeof(UIManager));
    if (!manager) return NULL;
    memset(manager, 0, sizeof(UIManager));

    if (pthread_mutex_init(&manager->mutex, NULL) != 0) { free(manager); return NULL; }
    if (initscr() == NULL) { pthread_mutex_destroy(&manager->mutex); free(manager); return NULL; }

    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(100); // getch()의 블로킹 방지를 위해 타임아웃 설정

    init_pair(1, COLOR_WHITE, COLOR_BLUE); // Title
    init_pair(2, COLOR_WHITE, COLOR_BLACK); // Menu
    init_pair(3, COLOR_BLACK, COLOR_WHITE); // Menu Selected
    init_pair(4, COLOR_WHITE, COLOR_GREEN); // Status OK
    init_pair(5, COLOR_WHITE, COLOR_RED);   // Status Error

    int term_h, term_w;
    getmaxyx(stdscr, term_h, term_w);
    if (term_h < 24 || term_w < 80) {
        endwin();
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        fprintf(stderr, "터미널 크기가 너무 작습니다 (최소 80x24 필요).\n");
        return NULL;
    }

    manager->main_win = stdscr;
    manager->menu_win = newwin(term_h - 4, term_w, 1, 0);
    manager->status_win = newwin(3, term_w, term_h - 3, 0);
    box(manager->menu_win, 0, 0);
    box(manager->status_win, 0, 0);

    return manager;
}

/**
 * @brief UI 관리자와 ncurses 관련 자원을 정리합니다.
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
 * @brief 상태바에 일반 메시지를 설정합니다.
 */
void set_status_message(UIManager* manager, const char* message) {
    if (!manager || !message) return;
    werase(manager->status_win);
    box(manager->status_win, 0, 0);
    wattron(manager->status_win, COLOR_PAIR(4));
    mvwprintw(manager->status_win, 1, 2, "STATUS: %s", message);
    wattroff(manager->status_win, COLOR_PAIR(4));
}

/**
 * @brief 상태바에 에러 메시지를 설정합니다.
 */
void set_error_message(UIManager* manager, const char* message) {
    if (!manager || !message) return;
    werase(manager->status_win);
    box(manager->status_win, 0, 0);
    wattron(manager->status_win, COLOR_PAIR(5));
    mvwprintw(manager->status_win, 1, 2, "ERROR: %s", message);
    wattroff(manager->status_win, COLOR_PAIR(5));
}