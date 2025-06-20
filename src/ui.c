/**
 * @file ui.c
 * @brief 사용자 인터페이스 모듈 - ncurses 기반 터미널 UI
 * @details 메뉴 시스템, 상태 표시, 에러/성공 메시지 출력 기능을 제공합니다.
 */

// src/ui.c

#include "../include/ui.h"  // UI 관련 헤더 파일 포함
#include "../include/message.h"  // 메시지 관련 헤더 파일 포함

/* --- 전역 변수 --- */
UIManager* global_ui_manager = NULL;  // 전역 UI 매니저 포인터

/* --- 내부(private) 함수 프로토타입 --- */
static void set_error_message(UIManager* manager, const char* message);  // 에러 메시지 설정 함수

/* --- 공개(public) 함수 정의 --- */

/**
 * @brief UI 시스템을 초기화합니다.
 * @return 성공 시 0, 실패 시 -1
 */
int init_ui(void) {
    setlocale(LC_ALL, ""); // 로케일 설정 (한글 지원)
    global_ui_manager = init_ui_manager();  // UI 매니저 초기화
    if (!global_ui_manager) {  // UI 매니저 초기화 실패 시
        utils_report_error(ERROR_UI_INIT_FAILED, "UI", "UI 매니저 초기화 실패");  // 에러 메시지 출력
        return -1;  // 에러 코드 반환
    }
    LOG_INFO("UI", "UI 시스템 초기화 성공");  // 정보 로그 출력
    return 0;  // 성공 코드 반환
}

/**
 * @brief UI 시스템의 메모리를 정리합니다.
 */
void cleanup_ui(void) {
    if (global_ui_manager) {  // 전역 UI 매니저가 존재하는 경우
        cleanup_ui_manager(global_ui_manager);  // UI 매니저 정리
        global_ui_manager = NULL;  // 포인터를 NULL로 설정
        LOG_INFO("UI", "UI 시스템 정리 완료");  // 정보 로그 출력
    }
}

/**
 * @brief 에러 메시지를 화면에 표시합니다.
 * @param message 표시할 에러 메시지
 */
void show_error_message(const char* message) {
    if (!global_ui_manager || !message) return;  // 유효성 검사
    set_error_message(global_ui_manager, message);  // 에러 메시지 설정
    refresh_all_windows();  // 모든 윈도우 새로고침
}

/**
 * @brief 성공 메시지를 화면에 표시합니다.
 * @param message 표시할 성공 메시지
 */
void show_success_message(const char* message) {
    if (!global_ui_manager || !message) return;  // 유효성 검사
    set_status_message(global_ui_manager, message);  // 상태 메시지 설정
    refresh_all_windows();  // 모든 윈도우 새로고침
}

/**
 * @brief 서버 상태 정보를 화면에 업데이트합니다.
 * @param session_count 활성 세션 수
 * @param port 서버 포트 번호
 */
void update_server_status(int session_count, int port) {
    if (!global_ui_manager) return;  // UI 매니저가 NULL이면 함수 종료
    char status_msg[MAX_MESSAGE_LENGTH];  // 상태 메시지 버퍼
    snprintf(status_msg, sizeof(status_msg), "Server Running on Port: %d | Active Sessions: %d", port, session_count);  // 상태 메시지 생성
    set_status_message(global_ui_manager, status_msg);  // 상태 메시지 설정
}

/**
 * @brief 서버의 장비 목록을 화면에 업데이트합니다.
 * @param devices 장비 배열
 * @param count 장비 개수
 * @param resource_manager 리소스 매니저 (예약 정보 조회용)
 * @param reservation_manager 예약 매니저 (예약 정보 조회용)
 */
void update_server_devices(const Device* devices, int count, ResourceManager* resource_manager, ReservationManager* reservation_manager){
    if (!global_ui_manager) {
        utils_report_error(ERROR_UI_INIT_FAILED, "UI", "update_server_devices: UI 매니저가 초기화되지 않음");
        return;
    }

    // LOG_INFO("UI", "서버 장비 목록 업데이트 시작: 장비수=%d", count);

    pthread_mutex_lock(&global_ui_manager->mutex);
    
    // 장비 목록 윈도우 지우기
    werase(global_ui_manager->menu_win);
    
    // 제목 표시
    mvwprintw(global_ui_manager->menu_win, 0, 2, " 장비 목록 ");
    
    if (count <= 0) {
        mvwprintw(global_ui_manager->menu_win, 2, 2, "등록된 장비가 없습니다.");
        // LOG_WARNING("UI", "등록된 장비가 없음");
    } else {
        // LOG_INFO("UI", "장비 목록 표시 시작: %d개 장비", count);
        
        // 헤더 표시
        mvwprintw(global_ui_manager->menu_win, 1, 2, "%-10s | %-25s | %-15s | %-20s | %-15s", 
                  "ID", "이름", "타입", "상태", "예약자");
        
        // 장비 목록 표시
        for (int i = 0; i < count && i < LINES - 10; i++) {
            const Device* device = &devices[i];
            
            // 예약 정보 조회
            char reservation_info[32] = "-";
            if (device->status == DEVICE_RESERVED) {
                Reservation* res = reservation_get_active_for_device(reservation_manager, resource_manager, device->id);
                if (res) {
                    time_t current_time = time(NULL);
                    long remaining_sec = (res->end_time > current_time) ? (res->end_time - current_time) : 0;
                    snprintf(reservation_info, sizeof(reservation_info), "%s (%lds)", res->username, remaining_sec);
                    // LOG_INFO("UI", "장비 %s 예약 정보: 사용자=%s, 남은시간=%ld초",
                    //          device->id, res->username, remaining_sec);
                } else {
                    // LOG_WARNING("UI", "장비 %s가 예약 상태이지만 예약 정보를 찾을 수 없음", device->id);
                }
            }
            
            // 상태 문자열 가져오기
            const char* status_str = message_get_device_status_string(device->status);
            
            // 장비 정보 표시
            mvwprintw(global_ui_manager->menu_win, i + 2, 2, "%-10s | %-25s | %-15s | %-20s | %-15s",
                      device->id, device->name, device->type, status_str, reservation_info);
            
            // LOG_INFO("UI", "장비 %d 표시: ID=%s, 이름=%s, 타입=%s, 상태=%s, 예약정보=%s",
            //          i, device->id, device->name, device->type, status_str, reservation_info);
        }
        
        // LOG_INFO("UI", "장비 목록 표시 완료: %d개 장비", count);
    }
    
    // 윈도우 테두리 그리기
    box(global_ui_manager->menu_win, 0, 0);
    
    // 윈도우 새로고침
    wrefresh(global_ui_manager->menu_win);
    
    pthread_mutex_unlock(&global_ui_manager->mutex);
    
    // LOG_INFO("UI", "서버 장비 목록 업데이트 완료");
}

/**
 * @brief 모든 윈도우를 새로고침합니다.
 */
void refresh_all_windows(void) {
    if (!global_ui_manager) return;  // UI 매니저가 NULL이면 함수 종료
    pthread_mutex_lock(&global_ui_manager->mutex);  // UI 매니저 뮤텍스 잠금
    if (global_ui_manager->main_win) wnoutrefresh(global_ui_manager->main_win);  // 메인 윈도우 새로고침
    if (global_ui_manager->status_win) wnoutrefresh(global_ui_manager->status_win);  // 상태 윈도우 새로고침
    if (global_ui_manager->menu_win) wnoutrefresh(global_ui_manager->menu_win);  // 메뉴 윈도우 새로고침
    doupdate();  // 화면 업데이트
    pthread_mutex_unlock(&global_ui_manager->mutex);  // UI 매니저 뮤텍스 해제
}

/**
 * @brief UI 매니저를 초기화합니다.
 * @return 성공 시 초기화된 UIManager 포인터, 실패 시 NULL
 */
UIManager* init_ui_manager(void) {
    UIManager* manager = (UIManager*)malloc(sizeof(UIManager));
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
    manager->menu_win = newwin(max_y - 2, max_x, 1, 0);
    manager->status_win = newwin(1, max_x, max_y - 1, 0);
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
void cleanup_ui_manager(UIManager* manager) {
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
void set_status_message(UIManager* manager, const char* message) {
    if (!manager || !message) return;  // 유효성 검사
    werase(manager->status_win);  // 상태 윈도우 내용 지우기
    box(manager->status_win, 0, 0);  // 상태 윈도우 테두리 그리기
    wattron(manager->status_win, COLOR_PAIR(4)); // 녹색 배경
    mvwprintw(manager->status_win, 1, 2, "STATUS: %s", message);  // 상태 메시지 출력
    wattroff(manager->status_win, COLOR_PAIR(4));  // 색상 해제
}

/**
 * @brief 에러 메시지를 설정합니다.
 * @param manager UI 매니저 포인터
 * @param message 표시할 에러 메시지
 */
void set_error_message(UIManager* manager, const char* message) {
    if (!manager || !message) return;  // 유효성 검사
    werase(manager->status_win);  // 상태 윈도우 내용 지우기
    box(manager->status_win, 0, 0);  // 상태 윈도우 테두리 그리기
    wattron(manager->status_win, COLOR_PAIR(5)); // 빨간색 배경
    mvwprintw(manager->status_win, 1, 2, "ERROR: %s", message);  // 에러 메시지 출력
    wattroff(manager->status_win, COLOR_PAIR(5));  // 색상 해제
}