#ifndef UI_H
#define UI_H

#include "common.h"
#include "resource.h"    // Device 정보 표시
#include "reservation.h" // Reservation 정보 표시
#include "session.h"     // ClientSession 정보 표시

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

/* UI 메뉴 항목 구조체 */
typedef struct ui_menu_item {
    char key;
    const char* description;
    void (*handler)(void);
} ui_menu_item_t;

/* UI 매니저 구조체 */
typedef struct ui_manager {
    WINDOW* main_win;
    WINDOW* status_win;
    WINDOW* menu_win;
    WINDOW* message_win;
    WINDOW* input_win;
    MENU* menu;
    ITEM** menu_items;
    FORM* form;
    FIELD** form_fields;
    bool is_running;
    pthread_mutex_t mutex;
    int menu_count;
    int field_count;
    menu_type_t current_menu;
    int selected_item;
    char status_message[MAX_MESSAGE_LENGTH];
    char error_message[MAX_MESSAGE_LENGTH];
    char success_message[MAX_MESSAGE_LENGTH];
} ui_manager_t;

/* 전역 UI 매니저 */
extern ui_manager_t* g_ui_manager;

/* UI 초기화 및 정리 함수 */
int ui_init(void);
void ui_cleanup(void);
void ui_refresh_all_windows(void);

/* 서버 UI 함수 */
void ui_update_server_status(int session_count, int port);
void ui_update_server_devices(const device_t* devices, int count, resource_manager_t* resource_manager, reservation_manager_t* reservation_manager);

ui_manager_t* ui_init_manager(void);
void ui_cleanup_manager(ui_manager_t* manager);
void ui_set_status_message(ui_manager_t* manager, const char* message);

void ui_show_error_message(const char* message);
void ui_show_success_message(const char* message);

#endif /* UI_H */