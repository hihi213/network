#include "../include/ui.h"

#include "../include/logger.h"
#include "../include/resource.h"
#include "../include/reservation.h"
#include "../include/session.h"



/* 전역 UIManager 포인터 */
UIManager* global_ui_manager = NULL;

// 예약 관련 함수 프로토타입 선언
void handle_device_reservation(ReservationManager* manager, const char* username);
void handle_reservation_cancel(ReservationManager* manager, const char* username);

/* UI 초기화 래퍼 함수 */
int init_ui(void) {
    LOG_INFO("UI", "init_ui 호출됨");
    setlocale(LC_ALL, "");
    global_ui_manager = init_ui_manager();
    if (!global_ui_manager) {
        LOG_ERROR("UI", "UI 매니저 초기화 실패");
        return -1;
    }
    LOG_INFO("UI", "UI 매니저 초기화 성공");
    return 0;
}

/* UI 정리 래퍼 함수 */
void cleanup_ui(void) {
    if (global_ui_manager) {
        cleanup_ui_manager(global_ui_manager);
        global_ui_manager = NULL;
    }
}

/* UI 매니저 초기화 */
UIManager* init_ui_manager(void) {
    LOG_INFO("UI", "init_ui_manager 호출됨");
    UIManager* manager = (UIManager*)malloc(sizeof(UIManager));
    if (!manager) {
        LOG_ERROR("UI", "UI 매니저 메모리 할당 실패");
        return NULL;
    }

    memset(manager, 0, sizeof(UIManager));

    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        LOG_ERROR("UI", "뮤텍스 초기화 실패");
        free(manager);
        return NULL;
    }

    if (initscr() == NULL) {
        LOG_ERROR("UI", "ncurses 초기화 실패");
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        return NULL;
    }

    if (start_color() == ERR) {
        LOG_ERROR("UI", "색상 초기화 실패");
        endwin();
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        return NULL;
    }

    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    init_pair(COLOR_PAIR_TITLE, COLOR_WHITE, COLOR_BLUE);
    init_pair(COLOR_PAIR_MENU, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_PAIR_MENU_SELECTED, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_PAIR_STATUS, COLOR_WHITE, COLOR_GREEN);
    init_pair(COLOR_PAIR_ERROR, COLOR_WHITE, COLOR_RED);
    init_pair(COLOR_PAIR_INFO, COLOR_WHITE, COLOR_CYAN);
    LOG_INFO("UI", "터미널 크기: COLS=%d, LINES=%d", COLS, LINES);

    if (COLS < 80 || LINES < 24) {
        LOG_ERROR("UI", "터미널 크기가 너무 작습니다 (최소: 80x24, 현재: %dx%d)", COLS, LINES);
        endwin();
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        return NULL;
    }

    manager->main_win = stdscr;
    manager->input_win = newwin(3, COLS, 0, 0);
    manager->menu_win = newwin(LINES - 6, COLS, 3, 0);
    box(manager->menu_win, 0, 0);
    manager->status_win = newwin(3, COLS, LINES - 3, 0);

    if (!manager->main_win || !manager->input_win || !manager->menu_win || !manager->status_win) {
        LOG_ERROR("UI", "윈도우 생성 실패");
        if(manager->status_win) delwin(manager->status_win);
        if(manager->menu_win) delwin(manager->menu_win);
        if(manager->input_win) delwin(manager->input_win);
        endwin();
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        return NULL;
    }

    manager->is_running = true;
    LOG_INFO("UI", "UI 매니저 초기화 완료");
    return manager;
}

/* UI 매니저 정리 */
void cleanup_ui_manager(UIManager* manager) {
    if (!manager) return;

    pthread_mutex_lock(&manager->mutex);

    if (manager->menu) {
        unpost_menu(manager->menu);
        free_menu(manager->menu);
    }
    if (manager->menu_items) {
        for (int i = 0; i < manager->menu_count; i++) {
            if(manager->menu_items[i]) free_item(manager->menu_items[i]);
        }
        free(manager->menu_items);
    }

    if (manager->form) {
        unpost_form(manager->form);
        free_form(manager->form);
    }
    if (manager->form_fields) {
        for (int i = 0; i < manager->field_count; i++) {
            if(manager->form_fields[i]) free_field(manager->form_fields[i]);
        }
        free(manager->form_fields);
    }

    delwin(manager->status_win);
    delwin(manager->menu_win);
    delwin(manager->input_win);
    endwin();

    pthread_mutex_unlock(&manager->mutex);
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

int create_menu(UIManager* manager, const char* title, const char** items, int count) {
    if (!manager || !items || count <= 0) {
        LOG_ERROR("UI", "createMenu: 잘못된 파라미터");
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);

    // 1. ncurses 메뉴 아이템 생성
    ITEM** menu_items = (ITEM**)calloc(count + 1, sizeof(ITEM*));
    if (!menu_items) {
        LOG_ERROR("UI", "메뉴 아이템 메모리 할당 실패");
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }
    for (int i = 0; i < count; i++) {
        menu_items[i] = new_item(items[i], "");
    }
    menu_items[count] = NULL; // 메뉴 아이템 배열은 NULL로 끝나야 함

    // 2. ncurses 메뉴 생성
    MENU* menu = new_menu(menu_items);
    
    // 3. 메뉴를 표시할 윈도우 설정
    werase(manager->menu_win);
    box(manager->menu_win, 0, 0);
    if (title) {
        mvwprintw(manager->menu_win, 0, 2, " %s ", title);
    }
    
    set_menu_win(menu, manager->menu_win);
    // 윈도우 크기에 맞게 서브 윈도우 생성 (스크롤 가능 영역)
    WINDOW* menu_sub = derwin(manager->menu_win, LINES - 8, COLS - 4, 1, 2);
    set_menu_sub(menu, menu_sub);
    set_menu_mark(menu, " > ");

    post_menu(menu);
    wrefresh(manager->menu_win);

    // 4. 사용자 입력 처리 루프
    int selected_index = -1;
    int ch;
    keypad(manager->menu_win, TRUE); // 키패드 입력 활성화
    while ((ch = wgetch(manager->menu_win))) {
        switch (ch) {
            case KEY_DOWN:
                menu_driver(menu, REQ_DOWN_ITEM);
                break;
            case KEY_UP:
                menu_driver(menu, REQ_UP_ITEM);
                break;
            case 27: // ESC 키
            case 'q':
            case 'Q':
                selected_index = -1;
                goto end_menu_loop; // 루프 종료
            case 10: // Enter 키
            case KEY_ENTER:
                {
                    ITEM* cur = current_item(menu);
                    selected_index = item_index(cur);
                    goto end_menu_loop; // 루프 종료
                }
                break;
        }
        wrefresh(manager->menu_win);
    }

end_menu_loop:
    // 5. 자원 정리
    unpost_menu(menu);
    free_menu(menu);
    for (int i = 0; i < count; i++) {
        free_item(menu_items[i]);
    }
    free(menu_items);
    delwin(menu_sub);
    werase(manager->menu_win); // 메뉴 윈도우 내용 지우기
    box(manager->menu_win, 0, 0);
    wrefresh(manager->menu_win);

    pthread_mutex_unlock(&manager->mutex);

    return selected_index;
}
/* 폼 생성 */
bool create_form(UIManager* manager, const char** labels, int count) {
    if (!manager || !labels || count <= 0) {
        LOG_ERROR("UI", "create_form: 잘못된 파라미터");
        return false;
    }

    pthread_mutex_lock(&manager->mutex);

    if (manager->form) {
        unpost_form(manager->form);
        free_form(manager->form);
        manager->form = NULL;
    }
    if (manager->form_fields) {
        for (int i = 0; i < manager->field_count; i++) {
            if (manager->form_fields[i]) free_field(manager->form_fields[i]);
        }
        free(manager->form_fields);
        manager->form_fields = NULL;
    }

    manager->form_fields = (FIELD**)calloc(count * 2 + 1, sizeof(FIELD*));
    if (!manager->form_fields) {
        LOG_ERROR("UI", "폼 필드 메모리 할당 실패");
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    int max_label_len = 0;
    for (int i = 0; i < count; i++) {
        if (labels[i]) {
            int len = strlen(labels[i]);
            if (len > max_label_len) max_label_len = len;
        }
    }

    for (int i = 0; i < count; i++) {
        manager->form_fields[i*2] = new_field(1, max_label_len, i, 0, 0, 0);
        set_field_buffer(manager->form_fields[i*2], 0, labels[i]);
        field_opts_off(manager->form_fields[i*2], O_ACTIVE | O_EDIT);

        manager->form_fields[i*2 + 1] = new_field(1, COLS - max_label_len - 10, i, max_label_len + 2, 0, 0);
        set_field_back(manager->form_fields[i*2 + 1], A_UNDERLINE);
        field_opts_off(manager->form_fields[i*2 + 1], O_AUTOSKIP);
    }
    manager->form_fields[count * 2] = NULL;

    manager->form = new_form(manager->form_fields);
    
    werase(manager->menu_win);
    box(manager->menu_win, 0, 0);
    set_form_win(manager->form, manager->menu_win);
    set_form_sub(manager->form, derwin(manager->menu_win, count + 2, COLS - 4, 1, 2));

    post_form(manager->form);
    manager->field_count = count * 2;
    
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/* 상태 메시지 설정 */
void set_status_message(UIManager* manager, const char* message) {
    if (!manager || !message) return;

    pthread_mutex_lock(&manager->mutex);
    strncpy(manager->status_message, message, MAX_MESSAGE_LENGTH - 1);
    manager->status_message[MAX_MESSAGE_LENGTH - 1] = '\0';
    
    werase(manager->status_win);
    box(manager->status_win, 0, 0);
    wattron(manager->status_win, COLOR_PAIR(COLOR_PAIR_STATUS));
    mvwprintw(manager->status_win, 1, 2, "%s", message);
    wattroff(manager->status_win, COLOR_PAIR(COLOR_PAIR_STATUS));
    
    pthread_mutex_unlock(&manager->mutex);
}

/* 에러 메시지 설정 */
void set_error_message(UIManager* manager, const char* message) {
    if (!manager || !message) return;

    pthread_mutex_lock(&manager->mutex);
    strncpy(manager->error_message, message, MAX_MESSAGE_LENGTH - 1);
    manager->error_message[MAX_MESSAGE_LENGTH - 1] = '\0';
    
    werase(manager->status_win);
    box(manager->status_win, 0, 0);
    wattron(manager->status_win, COLOR_PAIR(COLOR_PAIR_ERROR));
    mvwprintw(manager->status_win, 1, 2, "Error: %s", message);
    wattroff(manager->status_win, COLOR_PAIR(COLOR_PAIR_ERROR));
    
    pthread_mutex_unlock(&manager->mutex);
}

/* 폼 데이터 가져오기 */
bool get_form_data(UIManager* manager, char** data, int count) {
    if (!manager || !data || count <= 0 || !manager->form) {
        return false;
    }

    pthread_mutex_lock(&manager->mutex);
    for (int i = 0; i < count; i++) {
        char* field_data = field_buffer(manager->form_fields[i * 2 + 1], 0);
        strncpy(data[i], field_data, MAX_ARG_LENGTH - 1);
        data[i][MAX_ARG_LENGTH - 1] = '\0';
        
        // Trim trailing spaces
        int len = strlen(data[i]);
        while (len > 0 && data[i][len - 1] == ' ') {
            data[i][--len] = '\0';
        }
    }
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/* 클라이언트/서버 공용 UI 함수 */
void refresh_all_windows(void) {
    if (!global_ui_manager) return;

    pthread_mutex_lock(&global_ui_manager->mutex);
    
    if (global_ui_manager->main_win) wnoutrefresh(global_ui_manager->main_win);
    if (global_ui_manager->status_win) wnoutrefresh(global_ui_manager->status_win);
    if (global_ui_manager->menu_win) wnoutrefresh(global_ui_manager->menu_win);
    if (global_ui_manager->input_win) wnoutrefresh(global_ui_manager->input_win);

    doupdate();

    pthread_mutex_unlock(&global_ui_manager->mutex);
}

void show_error_message(const char* message) {
    if (!global_ui_manager || !message) return;
    set_error_message(global_ui_manager, message);
}

void show_success_message(const char* message) {
    if (!global_ui_manager || !message) return;
    set_status_message(global_ui_manager, message);
}

/* 클라이언트 UI 함수 */
void update_client_status(const ClientSession* session) {
    if (!global_ui_manager || !session) return;
    char status[256];
    snprintf(status, sizeof(status), "Status: %s | User: %s | Server: %s:%d",
             (session->state == SESSION_LOGGED_IN) ? "Logged In" : "Connected",
             (session->state == SESSION_LOGGED_IN) ? session->username : "N/A",
             session->server_ip, session->server_port);
    set_status_message(global_ui_manager, status);
}

/* 서버 UI 함수 */
int init_server_ui(void) {
    return init_ui();
}

void update_server_status(int session_count, int port) {
    if (!global_ui_manager) return;
    char status_msg[MAX_MESSAGE_LENGTH];
    snprintf(status_msg, sizeof(status_msg), "Server Running on Port: %d | Active Sessions: %d", port, session_count);
    set_status_message(global_ui_manager, status_msg);
}

// [수정된 최종 함수] 서버 장비 목록 표시
void update_server_devices(const Device* devices, int count) {
    if (!global_ui_manager || !devices) {
        return;
    }

    pthread_mutex_lock(&global_ui_manager->mutex);

    // 메뉴 윈도우에 장비 목록을 그림
    if (global_ui_manager->menu_win) {
        werase(global_ui_manager->menu_win);
        box(global_ui_manager->menu_win, 0, 0);

        // 헤더 출력
        wattron(global_ui_manager->menu_win, A_BOLD);
        mvwprintw(global_ui_manager->menu_win, 1, 2, "%-10s | %-25s | %-15s | %s", "ID", "Name", "Type", "Status");
        wattroff(global_ui_manager->menu_win, A_BOLD);

        // 구분선
        mvwaddstr(global_ui_manager->menu_win, 2, 2, "------------------------------------------------------------------");

        // 장비 목록 출력
        for (int i = 0; i < count; i++) {
            if (i + 3 >= LINES - 6) break; // 윈도우 크기 초과 방지
            
            const char* status_str = get_device_status_string(devices[i].status);
            mvwprintw(global_ui_manager->menu_win, i + 3, 2, "%-10s | %-25s | %-15s | %s",
                      devices[i].id,
                      devices[i].name,
                      devices[i].type,
                      status_str);
        }
    }
    pthread_mutex_unlock(&global_ui_manager->mutex);
}

/* 예약 메뉴 아이템 */
// static const char* reservation_menu_items[] = {
//     "예약 목록",
//     "예약 생성",
//     "예약 취소",
//     "뒤로 가기",
//     NULL
// };

/* 예약 폼 필드 */
static FIELD* reservation_form_fields[5];
static FORM* reservation_form;
static WINDOW* reservation_form_win;

/* 예약 폼 초기화 */
int init_reservation_form(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    // 예약 폼 윈도우 생성
    reservation_form_win = newwin(10, 50, (max_y - 10) / 2, (max_x - 50) / 2);
    box(reservation_form_win, 0, 0);
    mvwprintw(reservation_form_win, 0, 2, "장비 예약");
    
    // 폼 필드 생성
    reservation_form_fields[0] = new_field(1, 20, 2, 2, 0, 0);
    reservation_form_fields[1] = new_field(1, 20, 4, 2, 0, 0);
    reservation_form_fields[2] = new_field(1, 20, 6, 2, 0, 0);
    reservation_form_fields[3] = NULL;
    
    // 필드 설정
    set_field_back(reservation_form_fields[0], A_UNDERLINE);
    set_field_back(reservation_form_fields[1], A_UNDERLINE);
    set_field_back(reservation_form_fields[2], A_UNDERLINE);
    
    field_opts_off(reservation_form_fields[0], O_AUTOSKIP);
    field_opts_off(reservation_form_fields[1], O_AUTOSKIP);
    field_opts_off(reservation_form_fields[2], O_AUTOSKIP);
    
    // 폼 생성
    reservation_form = new_form(reservation_form_fields);
    set_form_win(reservation_form, reservation_form_win);
    
    // 라벨 추가
    mvwprintw(reservation_form_win, 2, 25, "장비 ID:");
    mvwprintw(reservation_form_win, 4, 25, "예약 시간(분):");
    mvwprintw(reservation_form_win, 6, 25, "사용 목적:");
    
    post_form(reservation_form);
    wrefresh(reservation_form_win);
    
    return 0;
}

/* 예약 폼 정리 */
void cleanup_reservation_form(void) {
    if (reservation_form) {
        unpost_form(reservation_form);
        free_form(reservation_form);
    }
    
    for (int i = 0; reservation_form_fields[i]; i++) {
        free_field(reservation_form_fields[i]);
    }
    
    if (reservation_form_win) {
        delwin(reservation_form_win);
    }
}

/* 예약 현황 리스트 표시 및 선택 함수 추가 */
int show_reservation_list_and_select(const Reservation* reservations, int count) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    WINDOW* list_win = newwin(17, 90, (max_y - 17) / 2, (max_x - 90) / 2);
    box(list_win, 0, 0);
    mvwprintw(list_win, 0, 2, "예약 현황 (장비ID/사용자/시작/종료/목적/상태)");
    int show_count = count > 10 ? 10 : count;
    time_t now = time(NULL);
    for (int i = 0; i < show_count; i++) {
        char start[32], end[32];
        strncpy(start, ctime(&reservations[i].start_time), 31); start[24] = '\0';
        strncpy(end, ctime(&reservations[i].end_time), 31); end[24] = '\0';
        // 예약 가능/불가 판정
        const char* avail = "예약불가";
        if (reservations[i].status == RESERVATION_APPROVED && reservations[i].end_time > now) {
            avail = "예약불가";
        } else {
            avail = "예약가능";
        }
        mvwprintw(list_win, 2 + i, 2, "%2d. %-10s | %-10s | %s ~ %s | %-20s | %s",
            i + 1,
            reservations[i].device_id,
            reservations[i].username,
            start,
            end,
            reservations[i].reason,
            avail);
    }
    mvwprintw(list_win, 14, 2, "예약할 장비 번호 선택(1-%d), q:취소", show_count);
    wrefresh(list_win);
    int ch, select = -1;
    while (1) {
        ch = wgetch(list_win);
        if (ch >= '1' && ch <= '0' + show_count) {
            select = ch - '1';
            break;
        } else if (ch == 'q' || ch == 'Q' || ch == 27) {
            select = -1;
            break;
        }
    }
    delwin(list_win);
    return select;
}

/* 예약 메뉴 표시 함수 수정 */
int show_reservation_menu_with_status(const Reservation* reservations, int count) {
    const char* items[] = {"장비 예약", "예약 취소", "예약 조회", "뒤로가기"};
    int n_items = sizeof(items) / sizeof(items[0]);
    int choice = 0;
    WINDOW* menu_win = newwin(10, 40, 5, 10);
    box(menu_win, 0, 0);
    keypad(menu_win, TRUE);
    mvwprintw(menu_win, 1, 2, "예약 메뉴");
    for (int i = 0; i < n_items; i++) {
        mvwprintw(menu_win, 3 + i, 4, "%d. %s", i + 1, items[i]);
    }
    wrefresh(menu_win);
    int ch;
    while (1) {
        ch = wgetch(menu_win);
        if (ch >= '1' && ch <= '0' + n_items) {
            choice = ch - '1';
            break;
        } else if (ch == 'q' || ch == 'Q' || ch == 27) {
            choice = n_items - 1; // 뒤로가기
            break;
        }
    }
    delwin(menu_win);
    // 예약 조회 선택 시 예약 현황 보여주고, 선택하면 상세 정보 표시
    if (choice == 2 && reservations && count > 0) {
        int sel = show_reservation_list_and_select(reservations, count);
        if (sel >= 0) {
            show_reservation_info(&reservations[sel]);
        }
    }
    return choice;
}

/* 예약 정보 표시 */
void show_reservation_info(const Reservation* reservation) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    WINDOW* info_win = newwin(8, 50, (max_y - 8) / 2, (max_x - 50) / 2);
    box(info_win, 0, 0);
    mvwprintw(info_win, 0, 2, "예약 정보");
    
    mvwprintw(info_win, 2, 2, "장비 ID: %s", reservation->device_id);
    mvwprintw(info_win, 3, 2, "사용자: %s", reservation->username);
    mvwprintw(info_win, 4, 2, "시작 시간: %s", ctime(&reservation->start_time));
    mvwprintw(info_win, 5, 2, "종료 시간: %s", ctime(&reservation->end_time));
    mvwprintw(info_win, 6, 2, "사용 목적: %s", reservation->reason);
    
    mvwprintw(info_win, 7, 2, "아무 키나 누르면 닫습니다...");
    wrefresh(info_win);
    
    getch();
    delwin(info_win);
}

void show_info_message(const char* message) {
    if (!global_ui_manager || !message) return;
    set_status_message(global_ui_manager, message);
    wrefresh(global_ui_manager->status_win);
}

/* 예약 처리 함수 */
bool process_reservation(ReservationManager* manager, const char* device_id, 
                        const char* username, time_t start_time, 
                        time_t end_time, const char* reason) {
    if (!manager || !device_id || !username || !reason) {
        show_error_message("잘못된 예약 정보입니다.");
        return false;
    }

    if (create_reservation(manager, device_id, username, start_time, end_time, reason)) {
        show_success_message("예약이 성공적으로 완료되었습니다.");
        return true;
    } else {
        show_error_message("예약 생성에 실패했습니다.");
        return false;
    }
}

/* 예약 현황 조회 및 처리 */
void handle_reservation_view(ReservationManager* manager, const char* username) {
    if (!manager || !username) {
        show_error_message("예약 조회에 실패했습니다.");
        return;
    }

    // 사용자의 예약 목록 조회
    Reservation reservations[MAX_RESERVATIONS];
    int count = get_user_reservations(manager, username, reservations, MAX_RESERVATIONS);
    
    if (count < 0) {
        show_error_message("예약 목록 조회에 실패했습니다.");
        return;
    }

    // 예약 메뉴 표시 및 처리
    int choice = show_reservation_menu_with_status(reservations, count);
    
    switch (choice) {
        case 0: // 장비 예약
            handle_device_reservation(manager, username);
            break;
        case 1: // 예약 취소
            handle_reservation_cancel(manager, username);
            break;
        case 2: // 예약 조회
            // 이미 처리됨
            break;
        default:
            break;
    }
}

/* 장비 예약 처리 */
void handle_device_reservation(ReservationManager* manager, const char* username) {
    if (!manager || !username) return;

    // 예약 폼 초기화
    if (init_reservation_form() != 0) {
        show_error_message("예약 폼 초기화에 실패했습니다.");
        return;
    }

    // 폼 입력 처리
    char device_id[MAX_DEVICE_ID_LEN];
    char duration_str[32];
    char reason[MAX_REASON_LEN];
    
    // 필드에서 데이터 읽기
    form_driver(reservation_form, REQ_FIRST_FIELD);
    form_driver(reservation_form, REQ_END_LINE);
    strncpy(device_id, field_buffer(reservation_form_fields[0], 0), MAX_DEVICE_ID_LEN - 1);
    device_id[MAX_DEVICE_ID_LEN - 1] = '\0';
    
    form_driver(reservation_form, REQ_NEXT_FIELD);
    form_driver(reservation_form, REQ_END_LINE);
    strncpy(duration_str, field_buffer(reservation_form_fields[1], 0), sizeof(duration_str) - 1);
    duration_str[sizeof(duration_str) - 1] = '\0';
    
    form_driver(reservation_form, REQ_NEXT_FIELD);
    form_driver(reservation_form, REQ_END_LINE);
    strncpy(reason, field_buffer(reservation_form_fields[2], 0), MAX_REASON_LEN - 1);
    reason[MAX_REASON_LEN - 1] = '\0';

    // 예약 시간 계산
    time_t now = time(NULL);
    int duration = atoi(duration_str);
    if (duration <= 0) {
        show_error_message("잘못된 예약 시간입니다.");
        cleanup_reservation_form();
        return;
    }

    time_t end_time = now + (duration * 60); // 분을 초로 변환

    // 예약 처리
    if (process_reservation(manager, device_id, username, now, end_time, reason)) {
        show_success_message("예약이 완료되었습니다.");
    }

    cleanup_reservation_form();
}

/* 예약 취소 처리 */
void handle_reservation_cancel(ReservationManager* manager, const char* username) {
    if (!manager || !username) return;

    // 사용자의 예약 목록 조회
    Reservation reservations[MAX_RESERVATIONS];
    int count = get_user_reservations(manager, username, reservations, MAX_RESERVATIONS);
    
    if (count <= 0) {
        show_error_message("취소할 예약이 없습니다.");
        return;
    }

    // 예약 목록 표시 및 선택
    int sel = show_reservation_list_and_select(reservations, count);
    if (sel < 0) return;

    // 선택된 예약 취소
    if (cancel_reservation(manager, reservations[sel].id, username)) {
        show_success_message("예약이 취소되었습니다.");
    } else {
        show_error_message("예약 취소에 실패했습니다.");
    }
}

int show_reservation_menu(void) {
    // 예약 현황 정보 없이 단순 메뉴만 보여줌
    return show_reservation_menu_with_status(NULL, 0);
}

/* 범용 메뉴 생성 함수 */
bool create_generic_menu(UIManager* manager, const char* title, const char** items, int count, 
                        void (*item_handler)(int index, void* data), void* user_data) {
    if (!manager || !items || count <= 0) {
        LOG_ERROR("UI", "create_generic_menu: 잘못된 파라미터");
        return false;
    }

    pthread_mutex_lock(&manager->mutex);

    // 기존 메뉴 정리
    if (manager->menu) {
        unpost_menu(manager->menu);
        free_menu(manager->menu);
        manager->menu = NULL;
    }
    if (manager->menu_items) {
        for (int i = 0; i < manager->menu_count; i++) {
            if (manager->menu_items[i]) free_item(manager->menu_items[i]);
        }
        free(manager->menu_items);
        manager->menu_items = NULL;
    }

    // 새 메뉴 아이템 생성
    manager->menu_items = (ITEM**)calloc(count + 1, sizeof(ITEM*));
    if (!manager->menu_items) {
        LOG_ERROR("UI", "메뉴 아이템 메모리 할당 실패");
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    for (int i = 0; i < count; i++) {
        manager->menu_items[i] = new_item(items[i], "");
    }
    manager->menu_items[count] = NULL;

    // 메뉴 생성 및 설정
    manager->menu = new_menu(manager->menu_items);
    set_menu_win(manager->menu, manager->menu_win);
    
    WINDOW* menu_sub = derwin(manager->menu_win, LINES - 8, COLS - 4, 1, 2);
    set_menu_sub(manager->menu, menu_sub);
    
    set_menu_mark(manager->menu, " > ");
    set_menu_fore(manager->menu, COLOR_PAIR(COLOR_PAIR_MENU_SELECTED));
    set_menu_back(manager->menu, COLOR_PAIR(COLOR_PAIR_MENU));
    
    // 메뉴 윈도우 설정
    werase(manager->menu_win);
    box(manager->menu_win, 0, 0);
    if (title) {
        mvwprintw(manager->menu_win, 0, 2, " %s ", title);
    }
    post_menu(manager->menu);
    manager->menu_count = count;

    // 메뉴 이벤트 처리
    int ch;
    while ((ch = wgetch(manager->menu_win)) != KEY_F(1)) {
        switch (ch) {
            case KEY_DOWN:
                menu_driver(manager->menu, REQ_DOWN_ITEM);
                break;
            case KEY_UP:
                menu_driver(manager->menu, REQ_UP_ITEM);
                break;
            case 10: // Enter
                {
                    ITEM* cur_item = current_item(manager->menu);
                    for (int i = 0; i < count; i++) {
                        if (manager->menu_items[i] == cur_item) {
                            if (item_handler) {
                                item_handler(i, user_data);
                            }
                            break;
                        }
                    }
                }
                break;
            case 27: // ESC
                pthread_mutex_unlock(&manager->mutex);
                return true;
        }
        wrefresh(manager->menu_win);
    }

    pthread_mutex_unlock(&manager->mutex);
    return true;
}