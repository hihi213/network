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
    WINDOW* input_win;
    MENU* menu;
    ITEM** menu_items;
    FORM* form;
    FIELD** form_fields;
    bool is_running;
    pthread_mutex_t mutex;
    int menu_count;
    int field_count;
    char status_message[MAX_MESSAGE_LENGTH];
    char error_message[MAX_MESSAGE_LENGTH];
} UIManager;

/* 전역 UI 매니저 */
extern UIManager* global_ui_manager;

/* UI 초기화 및 정리 함수 */
int init_ui(void);
void cleanup_ui(void);
int init_server_ui(void);
void refresh_all_windows(void);

/* 서버 UI 함수 */
void update_server_status(int session_count, int port);
void update_server_devices(const Device* devices, int count);
void append_server_log(const char* message);

/* 클라이언트 UI 함수 */
void update_client_status(const ClientSession* session);
void show_client_menu(const UIMenuItem* menu_items, int item_count);
char* get_user_input(const char* prompt);
void show_error_message(const char* message);
void show_success_message(const char* message);
void display_device_list(UIManager* manager, const Device* devices, int count);
void show_reservation_info(const Reservation* reservation);
int show_reservation_list_and_select(const Reservation* reservations, int count);
bool create_generic_menu(UIManager* manager, const char* title, const char** items, int count, 
                        void (*item_handler)(int index, void* data), void* user_data);

UIManager* init_ui_manager(void);
void cleanup_ui_manager(UIManager* manager);
int createMenu(UIManager* manager, const char* title, const char** items, int count);
#endif /* UI_H */