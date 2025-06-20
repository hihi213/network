#ifndef MESSAGE_H
#define MESSAGE_H

#include "common.h"
#include "resource.h" // Device 구조체 사용을 위해 포함
#include "reservation.h" // ReservationManager 구조체 사용을 위해 포함

/* 메시지 타입 열거형 */
typedef enum {
    MSG_LOGIN,
    MSG_LOGOUT,
    MSG_RESERVE_REQUEST,
    MSG_RESERVE_RESPONSE,
    MSG_CANCEL_REQUEST,
    MSG_CANCEL_RESPONSE,
    MSG_STATUS_REQUEST,
    MSG_STATUS_UPDATE,
    MSG_STATUS_RESPONSE,
    MSG_TIME_SYNC_REQUEST,
    MSG_TIME_SYNC_RESPONSE,
    MSG_PING,
    MSG_PONG,
    MSG_PING_RESPONSE,
    MSG_ERROR
} message_type_t;

/* 메시지 구조체 */
#define MAX_MESSAGE_LENGTH 1024
#define MAX_ARG_LENGTH 256

typedef struct message {
    message_type_t type;
    char data[MAX_MESSAGE_LENGTH];
    char* args[MAX_ARGS];
    int arg_count;
    int priority;
    error_code_t error_code; // utils.h의 error_code_t 사용
} message_t;

/* 메시지 버퍼 구조체 */
typedef struct message_buffer {
    char* buffer;
    size_t size;
    size_t position;
    bool is_reading;
} message_buffer_t;

/* 메시지 생성/소멸/파싱 함수 */
message_t* message_create(message_type_t type, const char* data);
const char* message_get_type_string(message_type_t type);
const char* message_get_device_status_string(device_status_t status);

/* 메시지 생성 헬퍼 함수 */
message_t* message_create_status_response(const device_t* devices, int device_count, resource_manager_t* resource_manager, reservation_manager_t* reservation_manager);
message_t* message_create_error(const char* error_msg);
message_t* message_create_error_with_code(error_code_t error_code, const char* error_msg);
message_t* message_create_reservation(const char* device_id, const char* duration_str);
message_t* message_receive(SSL* ssl);
message_t* message_create_login(const char* username, const char* password);
message_t* message_create_cancel(const char* device_id);
bool message_fill_status_response_args(message_t* msg, const device_t* devices, int count, resource_manager_t* rm, reservation_manager_t* rvm);
void message_destroy(message_t* msg);

/* 에러 코드 관련 함수 */
const char* message_get_error_string(error_code_t error_code);
error_code_t message_get_error_code_from_string(const char* error_str);
#endif /* MESSAGE_H */