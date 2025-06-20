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
typedef enum {
    MAIN_MENU,
    DEVICE_MENU,
    RESERVATION_MENU,
    SETTINGS_MENU
} MenuType;

/* UI 메시지 관련 상수 */
#define MAX_MESSAGE_LENGTH 1024
#define MAX_ARG_LENGTH 256

/* UI 메뉴 항목 구조체 */
typedef struct {
    char key;
    const char* description;
    void (*handler)(void);
} UIMenuItem;

/* UI 매니저 구조체 */
typedef struct {
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
    MenuType current_menu;
    int selected_item;
    char status_message[MAX_MESSAGE_LENGTH];
    char error_message[MAX_MESSAGE_LENGTH];
    char success_message[MAX_MESSAGE_LENGTH];
} UIManager;

/* 전역 UI 매니저 */
extern UIManager* global_ui_manager;

/* UI 초기화 및 정리 함수 */
int ui_init(void);
void ui_cleanup(void);
void ui_refresh_all_windows(void);

/* 서버 UI 함수 */
void ui_update_server_status(int session_count, int port);
void ui_update_server_devices(const Device* devices, int count, ResourceManager* resource_manager, ReservationManager* reservation_manager);

UIManager* ui_init_manager(void);
void ui_cleanup_manager(UIManager* manager);
void ui_set_status_message(UIManager* manager, const char* message);

void ui_show_error_message(const char* message);
void ui_show_success_message(const char* message);

#endif /* UI_H */