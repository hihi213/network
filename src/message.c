#include "../include/message.h"
#include "../include/reservation.h"
/**
 * @brief 두 개의 문자열 인자를 갖는 메시지를 생성하는 정적 헬퍼 함수.
 * @param type 메시지 타입.
 * @param arg1 첫 번째 인자 문자열.
 * @param arg2 두 번째 인자 문자열.
 * @return 생성된 Message 객체 포인터, 실패 시 NULL.
 */
static Message* create_message_with_two_args(MessageType type, const char* arg1, const char* arg2) {
    Message* msg = create_message(type, NULL);
    if (!msg) {
        return NULL;
    }

    msg->args[0] = strdup(arg1);
    msg->args[1] = strdup(arg2);

    if (!msg->args[0] || !msg->args[1]) {
        cleanup_message(msg);
        free(msg);
        return NULL;
    }
    msg->arg_count = 2;

    return msg;
}
/* 메시지 생성 함수 */
Message *create_message(MessageType type, const char *data)
{
    Message *msg = (Message *)malloc(sizeof(Message));
    if (!msg)
    {
        LOG_ERROR("Message", "메시지 메모리 할당 실패");
        return NULL;
    }

    msg->type = type;
    msg->arg_count = 0;
    memset(msg->args, 0, sizeof(msg->args));

    if (data)
    {
        strncpy(msg->data, data, MAX_MESSAGE_LENGTH - 1);
        msg->data[MAX_MESSAGE_LENGTH - 1] = '\0';
    }
    else
    {
        msg->data[0] = '\0';
    }

    return msg;
}


bool fill_status_response_args(Message* msg, const Device* devices, int count, ResourceManager* rm, ReservationManager* rvm) {
    if (!msg || !devices || !rm || !rvm) return false;

    msg->arg_count = 0;
    for (int i = 0; i < count; i++) {
        int base_idx = i * 5;
        if (base_idx + 4 >= MAX_ARGS) break;

        const char* status_str = get_device_status_string(devices[i].status);
        char end_time_str[32] = "0";

        if (devices[i].status == DEVICE_RESERVED) {
            Reservation* res = get_active_reservation_for_device(rvm, rm, devices[i].id);
            if (res) {
                snprintf(end_time_str, sizeof(end_time_str), "%ld", res->end_time);
            }
        }

        msg->args[base_idx]     = strdup(devices[i].id);
        msg->args[base_idx + 1] = strdup(devices[i].name);
        msg->args[base_idx + 2] = strdup(devices[i].type);
        msg->args[base_idx + 3] = strdup(status_str);
        msg->args[base_idx + 4] = strdup(end_time_str);

        if (!msg->args[base_idx] || !msg->args[base_idx+1] || !msg->args[base_idx+2] || !msg->args[base_idx+3] || !msg->args[base_idx+4]) {
            return false; // 실패 시 false 반환
        }
        msg->arg_count += 5;
    }
    return true; // 성공 시 true 반환
}

/**
 * @brief 장비 목록으로 상태 응답 메시지를 생성합니다. (종료 시각 타임스탬프 전송 기능 추가)
 * @param devices 장비 목록 배열.
 * @param device_count 장비 개수.
 * @param reservation_manager 예약 관리자 포인터 (예약 정보 조회를 위해 필요).
 * @return 생성된 Message 객체 포인터.
 */
Message *create_status_response_message(const Device *devices, int device_count, ResourceManager* resource_manager, ReservationManager* reservation_manager) {
    Message *message = create_message(MSG_STATUS_RESPONSE, NULL);
    if (!message) return NULL;

    if (!fill_status_response_args(message, devices, device_count, resource_manager, reservation_manager)) {
        cleanup_message(message);
        free(message);
        return NULL;
    }

    return message;
}
/**
 * @brief 로그인 요청 메시지를 생성합니다.
 * @param username 사용자 이름.
 * @param password 비밀번호.
 * @return 생성된 Message 객체 포인터.
 */
Message* create_login_message(const char* username, const char* password) {
    return create_message_with_two_args(MSG_LOGIN, username, password); //
}

Message* create_reservation_message(const char *device_id, const char* duration_str) {
    return create_message_with_two_args(MSG_RESERVE_REQUEST, device_id, duration_str); //
}
/**
 * @brief 에러 메시지 객체를 생성합니다.
 * @param error_message 에러 메시지 문자열.
 * @return 생성된 Message 객체 포인터.
 */
Message* create_error_message(const char* error_message)
{
    // 내부적으로는 data 필드에 에러 메시지를 담는 MSG_ERROR 타입 메시지를 생성합니다.
    return create_message(MSG_ERROR, error_message);
}

/* 메시지 타입 문자열 변환 함수 */
const char *get_message_type_string(MessageType type)
{
    switch (type)
    {
    case MSG_LOGIN:
        return "LOGIN";
    case MSG_LOGOUT:
        return "LOGOUT";
    case MSG_RESERVE_REQUEST:
        return "RESERVE_REQUEST";
    case MSG_RESERVE_RESPONSE:
        return "RESERVE_RESPONSE";
    case MSG_CANCEL_REQUEST:
        return "CANCEL_REQUEST";
    case MSG_CANCEL_RESPONSE:
        return "CANCEL_RESPONSE";
    case MSG_STATUS_REQUEST:
        return "STATUS_REQUEST";
    case MSG_STATUS_RESPONSE:
        return "STATUS_RESPONSE";
    case MSG_PING:
        return "PING";
    case MSG_PONG:
        return "PONG";
    case MSG_PING_RESPONSE:
        return "PING_RESPONSE";
    case MSG_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

/* 메시지 정리 함수 (수정된 최종 버전) */
void cleanup_message(Message *msg)
{
    if (!msg)
        return;
    for (int i = 0; i < msg->arg_count; i++)
    {
        if (msg->args[i])
        {
            free(msg->args[i]);
            msg->args[i] = NULL;
        }
    }
}

// 인자들을 읽는 static 헬퍼 함수
static bool read_message_arguments(SSL* ssl, Message* msg, uint32_t arg_count) {
    for (uint32_t i = 0; i < arg_count; i++) {
        uint32_t arg_len;
        if (SSL_read(ssl, &arg_len, sizeof(arg_len)) <= 0) return false;
        arg_len = ntohl(arg_len);

        if (arg_len >= MAX_ARG_LENGTH) return false;

        msg->args[i] = (char*)malloc(arg_len + 1);
        if (!msg->args[i]) return false;

        if (SSL_read(ssl, msg->args[i], arg_len) <= 0) return false;
        
        msg->args[i][arg_len] = '\0';
        msg->arg_count++;
    }
    return true;
}

// 데이터를 읽는 static 헬퍼 함수
static bool read_message_data(SSL* ssl, Message* msg) {
    uint32_t data_len;
    if (SSL_read(ssl, &data_len, sizeof(data_len)) <= 0) return false;
    data_len = ntohl(data_len);

    if (data_len > 0) {
        if (data_len >= MAX_MESSAGE_LENGTH) return false;
        if (SSL_read(ssl, msg->data, data_len) <= 0) return false;
        msg->data[data_len] = '\0';
    }
    return true;
}

// 훨씬 간결해진 receive_message 함수
Message* receive_message(SSL* ssl) {
    uint32_t type, arg_count;

    if (SSL_read(ssl, &type, sizeof(type)) <= 0) return NULL;
    type = ntohl(type);

    if (SSL_read(ssl, &arg_count, sizeof(arg_count)) <= 0) return NULL;
    arg_count = ntohl(arg_count);
    
    Message* message = create_message(type, NULL);
    if (!message) return NULL;

    if (!read_message_arguments(ssl, message, arg_count)) {
        cleanup_message(message);
        free(message);
        return NULL;
    }

    if (!read_message_data(ssl, message)) {
        cleanup_message(message);
        free(message);
        return NULL;
    }
    
    LOG_INFO("Message", "메시지 수신 완료: 타입=%s", get_message_type_string(message->type));
    return message;
}

/* DeviceStatus를 문자열로 변환하는 함수 */
const char *get_device_status_string(DeviceStatus status)
{
    switch (status)
    {
    case DEVICE_AVAILABLE:
        return "available";
    case DEVICE_RESERVED:
        return "reserved";
    case DEVICE_MAINTENANCE:
        return "maintenance";
    default:
        return "unknown";
    }
}
