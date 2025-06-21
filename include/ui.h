#ifndef UI_H
#define UI_H

#include "common.h"
#include "resource.h"    // Device 정보 표시
#include "reservation.h" // Reservation 정보 표시
#include "session.h"     // ClientSession 정보 표시
#include <ncurses.h>
#include <pthread.h>

/* UI 상수 */
#define COLOR_PAIR_TITLE 1
#define COLOR_PAIR_MENU 2
#define COLOR_PAIR_MENU_SELECTED 3
#define COLOR_PAIR_STATUS 4
#define COLOR_PAIR_ERROR 5
#define COLOR_PAIR_INFO 6

/* UI 메뉴 타입 */
typedef enum menu_type {
    MAIN_MENU,
    DEVICE_MENU,
    RESERVATION_MENU,
    SETTINGS_MENU
} menu_type_t;

/* UI 메시지 관련 상수 */
#define MAX_MESSAGE_LENGTH 1024
#define MAX_ARG_LENGTH 256

/* UI 모드 정의 */
typedef enum {
    UI_CLIENT,
    UI_SERVER
} ui_mode_t;

/* 메뉴 아이템 구조체 */
typedef struct {
    const char* text;           // 메뉴 텍스트
    int id;                     // 메뉴 ID (선택 시 반환될 값)
    bool enabled;               // 활성화 여부
    void (*action)(void);       // 선택 시 실행할 콜백 함수 (선택사항)
} ui_menu_item_t;

/* 메뉴 구조체 */
typedef struct {
    const char* title;          // 메뉴 제목
    ui_menu_item_t* items;      // 메뉴 아이템 배열
    int item_count;             // 아이템 개수
    int highlight_index;        // 현재 하이라이트된 아이템 인덱스
    const char* help_text;      // 도움말 텍스트
} ui_menu_t;

/* UI 매니저 구조체 */
typedef struct {
    ui_mode_t mode;
    WINDOW* main_win;
    WINDOW* menu_win;
    WINDOW* status_win;
    WINDOW* message_win;
    pthread_mutex_t mutex;
} ui_manager_t;

/* 전역 UI 매니저 */
extern ui_manager_t* g_ui_manager;

/* UI 초기화 및 정리 함수 */
int ui_init(ui_mode_t mode);
void ui_cleanup(void);
void ui_handle_resize(void);
void ui_show_status(const char* msg);
void ui_show_error(const char* msg);
void ui_refresh_all_windows(void);

void client_draw_ui_for_current_state(void);
void server_draw_ui_for_current_state(void);

int get_display_width(const char* str);
void print_fixed_width(WINDOW* win, int y, int x, const char* str, int width);

void ui_show_message(const char* prefix, const char* message, int color_pair);

void ui_show_error_message(const char* message);

void ui_show_success_message(const char* message);

// 장비 목록 테이블 그리기 함수
void ui_draw_device_table(WINDOW* win, device_t* devices, int count, int highlight_row, 
                         bool show_remaining_time, void* reservation_manager, void* resource_manager, 
                         time_t current_time, bool use_color);

// 메뉴 렌더링 함수들
void ui_render_menu(WINDOW* win, const ui_menu_t* menu);

#endif /* UI_H */