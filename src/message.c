#include "../include/message.h"


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

Message *create_status_response_message(const Device *devices, int device_count)
{
    Message *message = create_message(MSG_STATUS_RESPONSE, NULL);
    if (!message)
        return NULL;

    message->arg_count = 0;
    for (int i = 0; i < device_count && i < MAX_DEVICES; i++)
    {
        int base_idx = i * 4;
        if (base_idx + 3 >= MAX_ARGS)
            break;
        // ID
        message->args[base_idx] = strdup(devices[i].id);
        // Name
        message->args[base_idx + 1] = strdup(devices[i].name);
        // Type
        message->args[base_idx + 2] = strdup(devices[i].type);
        // Status
        message->args[base_idx + 3] = strdup(get_device_status_string(devices[i].status));
        message->arg_count += 4;
    }
    return message;
}/**
 * @brief 로그인 요청 메시지를 생성합니다.
 * @param username 사용자 이름.
 * @param password 비밀번호.
 * @return 생성된 Message 객체 포인터.
 */
Message* create_login_message(const char* username, const char* password) { //
    Message* msg = create_message(MSG_LOGIN, NULL); //
    if (!msg) {
        return NULL;
    }

    msg->args[0] = strdup(username); //
    msg->args[1] = strdup(password); //

    // strdup 실패 시 메모리 정리
    if (!msg->args[0] || !msg->args[1]) {
        cleanup_message(msg);
        free(msg);
        return NULL;
    }
    msg->arg_count = 2; //

    return msg;
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
// 개선된 함수: 예약 시간(duration)을 함께 처리
/**
 * @brief 예약 요청 메시지를 생성합니다.
 * @param device_id 예약할 장비의 ID.
 * @param duration_str 예약할 시간(초)을 나타내는 문자열.
 * @return 생성된 Message 객체 포인터.
 */
Message *create_reservation_message(const char *device_id, const char* duration_str)
{
    Message *msg = create_message(MSG_RESERVE_REQUEST, NULL);
    if (!msg)
    {
        return NULL;
    }

    // 인자에 장비 ID와 예약 시간(초) 추가
    msg->args[0] = strdup(device_id);
    msg->args[1] = strdup(duration_str);
    if (!msg->args[0] || !msg->args[1]) {
        // strdup 실패 시 메모리 정리
        cleanup_message(msg);
        free(msg);
        return NULL;
    }
    msg->arg_count = 2;

    return msg;
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
