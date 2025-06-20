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
    int priority; // 우선순위 필드 추가
} Message;

/* 메시지 버퍼 구조체 */
typedef struct {
    char* buffer;
    size_t size;
    size_t position;
    bool is_reading;
} MessageBuffer;

/* 메시지 생성/소멸/파싱 함수 */
Message* message_create(MessageType type, const char* data);
const char* message_get_type_string(MessageType type);
const char* message_get_device_status_string(DeviceStatus status);

/* 메시지 생성 헬퍼 함수 */
Message *message_create_status_response(const Device *devices, int device_count, ResourceManager* resource_manager, ReservationManager* reservation_manager);
Message* message_create_error(const char* error_msg);
Message *message_create_reservation(const char *device_id, const char* duration_str);
Message* message_receive(SSL* ssl);
Message* message_create_login(const char* username, const char* password);
Message* message_create_cancel(const char* device_id);
/* DeviceStatus를 문자열로 변환하는 함수 */
const char* message_get_device_status_string(DeviceStatus status);
bool message_fill_status_response_args(Message* msg, const Device* devices, int count, ResourceManager* rm, ReservationManager* rvm) ;
void message_destroy(Message *msg);
#endif /* MESSAGE_H */