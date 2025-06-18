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
    MSG_STATUS_UPDATE, // [추가] 서버가 먼저 보내는 상태 업데이트 메시지
    MSG_STATUS_RESPONSE,
    MSG_PING,
    MSG_PONG,
    MSG_PING_RESPONSE,
    MSG_ERROR
} MessageType;

/* 메시지 구조체 */
#define MAX_MESSAGE_LENGTH 1024
#define MAX_ARG_LENGTH 256

typedef struct {
    MessageType type;
    char data[MAX_MESSAGE_LENGTH];
    char* args[MAX_ARGS];
    int arg_count;
} Message;

/* 메시지 버퍼 구조체 */
typedef struct {
    char* buffer;
    size_t size;
    size_t position;
    bool is_reading;
} MessageBuffer;

/* 메시지 생성/소멸/파싱 함수 */
Message* create_message(MessageType type, const char* data);
void cleanup_message(Message* message);
int parse_message(const char* buffer, size_t len, Message* message);
int serialize_message(const Message* message, char* buffer, size_t buffer_size);
const char* get_message_type_string(MessageType type);
const char* get_device_status_string(DeviceStatus status);

/* 메시지 생성 헬퍼 함수 */
Message *create_status_response_message(const Device *devices, int device_count, ResourceManager* resource_manager, ReservationManager* reservation_manager);
Message* create_error_message(const char* error_msg);
Message *create_reservation_message(const char *device_id, const char* duration_str);
Message* receive_message(SSL* ssl);
Message* create_login_message(const char* username, const char* password);
/* 메시지 유효성 검사 함수 */
bool is_valid_message(const Message* message);
bool validate_message_args(const Message* message, int expected_args);
/* DeviceStatus를 문자열로 변환하는 함수 */
const char* get_device_status_string(DeviceStatus status);
bool fill_status_response_args(Message* msg, const Device* devices, int count, ResourceManager* rm, ReservationManager* rvm) ;
#endif /* MESSAGE_H */