#include "../include/message.h"
#include "../include/logger.h"


/* 메시지 생성 함수 */
Message* create_message(MessageType type, const char* data) {
    Message* msg = (Message*)malloc(sizeof(Message));
    if (!msg) {
        LOG_ERROR("Message", "메시지 메모리 할당 실패");
        return NULL;
    }

    msg->type = type;
    msg->arg_count = 0;
    memset(msg->args, 0, sizeof(msg->args));

    if (data) {
        strncpy(msg->data, data, MAX_MESSAGE_LENGTH - 1);
        msg->data[MAX_MESSAGE_LENGTH - 1] = '\0';
    } else {
        msg->data[0] = '\0';
    }

    return msg;
}

Message* create_reserve_request_message(const char* device_id, time_t start_time,
                                     time_t end_time, const char* reason) {
    Message* msg = create_message(MSG_RESERVE_REQUEST, NULL);
    if (!msg) return NULL;
    
    // strdup 대신 strncpy를 사용하여 배열에 안전하게 복사
    strncpy(msg->args[0], device_id, MAX_ARG_LENGTH - 1);
    msg->args[0][MAX_ARG_LENGTH - 1] = '\0';
    
    strncpy(msg->args[1], ctime(&start_time), MAX_ARG_LENGTH - 1);
    msg->args[1][MAX_ARG_LENGTH - 1] = '\0';
    
    strncpy(msg->args[2], ctime(&end_time), MAX_ARG_LENGTH - 1);
    msg->args[2][MAX_ARG_LENGTH - 1] = '\0';

    strncpy(msg->args[3], reason, MAX_ARG_LENGTH - 1);
    msg->args[3][MAX_ARG_LENGTH - 1] = '\0';

    msg->arg_count = 4;
    
    return msg;
}

Message* create_reserve_response_message(uint32_t reservation_id, bool success,
                                      const char* message) {
    Message* msg = create_message(MSG_RESERVE_RESPONSE, NULL);
    if (!msg) return NULL;

    snprintf(msg->args[0], MAX_ARG_LENGTH, "%s", success ? "success" : "failure");
    snprintf(msg->args[1], MAX_ARG_LENGTH, "%u", reservation_id);
    strncpy(msg->args[2], message, MAX_ARG_LENGTH - 1);
    msg->args[2][MAX_ARG_LENGTH - 1] = '\0';
    msg->arg_count = 3;

    return msg;
}

Message* create_status_request_message(void) {
    return create_message(MSG_STATUS_REQUEST, NULL);
}

Message* create_status_response_message(const Device* devices, int device_count) {
    Message* message = create_message(MSG_STATUS_RESPONSE, NULL);
    if (!message) return NULL;

    message->arg_count = 0;
    for (int i = 0; i < device_count && i < MAX_DEVICES; i++) {
        int base_idx = i * 4;
        if (base_idx + 3 >= MAX_ARGS) break;
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
}

Message* create_error_message(const char* error_message) {
    Message* message = create_message(MSG_ERROR, error_message);
    if (!message) return NULL;
    return message;
}

Message* create_ping_message(void) {
    LOG_INFO("Message", "Ping 메시지 생성");
    Message* message = create_message(MSG_PING, NULL);
    if (!message) return NULL;
    message->arg_count = 0;
    return message;
}

Message* create_ping_response_message(void) {
    LOG_INFO("Message", "Ping 응답 메시지 생성");
    Message* message = create_message(MSG_PING_RESPONSE, NULL);
    if (!message) return NULL;
    message->arg_count = 0;
    return message;
}

/* 예약 요청 메시지 생성 */
Message* create_reservation_message(const char* device_id) {
    Message* msg = create_message(MSG_RESERVE_REQUEST, NULL);
    if (!msg) return NULL;

    msg->args[msg->arg_count++] = strdup(device_id);
    return msg;
}

/* 메시지 파싱 함수 */
int parse_message(const char* buffer, size_t len, Message* message) {
    (void)len; // unused
    if (!buffer || !message) return -1;
    char type_str[16] = {0};
    // 개행 전까지 읽도록 포맷 수정
    sscanf(buffer, "%15[^|]|%1023[^\n]", type_str, message->data);
    message->type = atoi(type_str);
    message->arg_count = 0;
    // 실제 구현은 필요에 따라 확장
    return 0;
}

/* 메시지 직렬화/역직렬화 함수 */
int serialize_message(const Message* message, char* buffer, size_t buffer_size) {
    if (!message || !buffer) {
        LOG_ERROR("Message", "잘못된 파라미터");
        return -1;
    }

    size_t offset = snprintf(buffer, buffer_size, "%d", message->type);
    if (offset >= buffer_size) {
        LOG_ERROR("Message", "버퍼 오버플로우");
        return -1;
    }

    for (int i = 0; i < message->arg_count; i++) {
        int len = snprintf(buffer + offset, buffer_size - offset, "|%s",
                         message->args[i]);
        if (len < 0 || (size_t)len > buffer_size - offset) {
            LOG_ERROR("Message", "버퍼 오버플로우");
            return -1;
        }
        offset += (size_t)len;
    }

    if (message->data[0] != '\0') {
        int len = snprintf(buffer + offset, buffer_size - offset, "|%s",
                         message->data);
        if (len < 0 || (size_t)len >= buffer_size - offset) {
            LOG_ERROR("Message", "버퍼 오버플로우");
            return -1;
        }
        offset += (size_t)len;
    }

    return (int)offset;
}

Message* deserialize_message(const char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return NULL;
    Message* message = (Message*)malloc(sizeof(Message));
    if (!message) return NULL;
    if (parse_message(buffer, buffer_size, message) != 0) {
        free(message);
        return NULL;
    }
    return message;
}

/* 메시지 유효성 검사 함수 */
bool is_valid_message(const Message* message) {
    if (!message) return false;
    switch (message->type) {
        case MSG_STATUS_REQUEST:
            return message->arg_count == 0;
        case MSG_STATUS_RESPONSE:
            return message->arg_count >= 0;
        case MSG_RESERVE_REQUEST:
            return message->arg_count == 1;
        case MSG_RESERVE_RESPONSE:
            return message->arg_count == 1;
        case MSG_ERROR:
            return true;
        case MSG_PING:
        case MSG_PING_RESPONSE:
            return message->arg_count == 0;
        default:
            return false;
    }
}

bool validate_message_args(const Message* message, int expected_args) {
    return message && message->arg_count == expected_args;
}

/* 메시지 타입 문자열 변환 함수 */
const char* get_message_type_string(MessageType type) {
    switch (type) {
        case MSG_LOGIN: return "LOGIN";
        case MSG_LOGOUT: return "LOGOUT";
        case MSG_RESERVE_REQUEST: return "RESERVE_REQUEST";
        case MSG_RESERVE_RESPONSE: return "RESERVE_RESPONSE";
        case MSG_CANCEL_REQUEST: return "CANCEL_REQUEST";
        case MSG_CANCEL_RESPONSE: return "CANCEL_RESPONSE";
        case MSG_STATUS_REQUEST: return "STATUS_REQUEST";
        case MSG_STATUS_RESPONSE: return "STATUS_RESPONSE";
        case MSG_PING: return "PING";
        case MSG_PONG: return "PONG";
        case MSG_PING_RESPONSE: return "PING_RESPONSE";
        case MSG_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

/* 메시지 정리 함수 (수정된 최종 버전) */
void cleanup_message(Message* msg) {
    if (!msg) return;
    for (int i = 0; i < msg->arg_count; i++) {
        if (msg->args[i]) {
            free(msg->args[i]);
            msg->args[i] = NULL;
        }
    }
}

/* 메시지 타입 문자열 변환 */
const char* message_type_to_string(int type) {
    switch (type) {
        case MSG_STATUS_REQUEST: return "STATUS_REQUEST";
        case MSG_STATUS_RESPONSE: return "STATUS_RESPONSE";
        case MSG_RESERVE_REQUEST: return "RESERVE_REQUEST";
        case MSG_RESERVE_RESPONSE: return "RESERVE_RESPONSE";
        case MSG_ERROR: return "ERROR";
        case MSG_PING: return "PING";
        case MSG_PONG: return "PONG";
        default: return "UNKNOWN";
    }
}

/* 버퍼에 데이터 쓰기 */
int write_to_buffer(MessageBuffer* buffer, const void* data, size_t offset, size_t len) {
    if (!buffer || !data) {
        LOG_ERROR("Message", "잘못된 파라미터");
        return -1;
    }

    size_t buffer_size = buffer->size;
    if (offset >= buffer_size) {
        LOG_ERROR("Message", "잘못된 오프셋");
        return -1;
    }

    if (len == 0) {
        return 0;
    }

    if (buffer->is_reading) {
        if (offset + len > buffer_size) {
            LOG_ERROR("Message", "버퍼 범위 초과");
            return -1;
        }
        memcpy(buffer->buffer + offset, data, len);
    } else {
        if (offset + len > buffer_size) {
            LOG_ERROR("Message", "버퍼 범위 초과");
            return -1;
        }
        memcpy(buffer->buffer + offset, data, len);
    }

    return (int)len;
}

Message* receive_message(SSL* ssl) {
    if (!ssl) {
        LOG_ERROR("Message", "잘못된 SSL 연결");
        return NULL;
    }

    LOG_INFO("Message", "메시지 수신 시작");

    // SSL 연결 상태 확인
    int ssl_state = SSL_get_state(ssl);
    LOG_INFO("Message", "현재 SSL 상태: %d", ssl_state);

    // SSL 연결이 유효한지 확인 (TLS_ST_OK = 3)
    if (ssl_state != TLS_ST_OK) {
        LOG_ERROR("Message", "SSL 연결이 유효하지 않음 (상태: %d)", ssl_state);
        return NULL;
    }

    // SSL 세션이 유효한지 확인
    if (!SSL_is_init_finished(ssl)) {
        LOG_ERROR("Message", "SSL 핸드셰이크가 완료되지 않음");
        return NULL;
    }

    // SSL 세션 정보 로깅
    const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl);
    if (cipher) {
        LOG_INFO("Message", "현재 사용 중인 암호화 방식: %s", SSL_CIPHER_get_name(cipher));
    }

    // 메시지 타입 수신
    uint32_t type;
    int ret = SSL_read(ssl, &type, sizeof(type));
    if (ret <= 0) {
        int ssl_error = SSL_get_error(ssl, ret);
        LOG_ERROR("Message", "메시지 타입 수신 실패 (SSL 에러: %d, 리턴값: %d)", ssl_error, ret);
        
        // SSL 에러 상세 정보 로깅
        switch (ssl_error) {
            case SSL_ERROR_NONE:
                LOG_ERROR("Message", "SSL_ERROR_NONE");
                break;
            case SSL_ERROR_ZERO_RETURN:
                LOG_ERROR("Message", "SSL_ERROR_ZERO_RETURN: 연결이 정상적으로 종료됨");
                break;
            case SSL_ERROR_WANT_READ:
                LOG_ERROR("Message", "SSL_ERROR_WANT_READ: 더 많은 데이터를 읽어야 함");
                break;
            case SSL_ERROR_WANT_WRITE:
                LOG_ERROR("Message", "SSL_ERROR_WANT_WRITE: 더 많은 데이터를 써야 함");
                break;
            case SSL_ERROR_WANT_CONNECT:
                LOG_ERROR("Message", "SSL_ERROR_WANT_CONNECT: 연결이 필요함");
                break;
            case SSL_ERROR_WANT_ACCEPT:
                LOG_ERROR("Message", "SSL_ERROR_WANT_ACCEPT: 수락이 필요함");
                break;
            case SSL_ERROR_WANT_X509_LOOKUP:
                LOG_ERROR("Message", "SSL_ERROR_WANT_X509_LOOKUP: 인증서 조회가 필요함");
                break;
            case SSL_ERROR_SYSCALL:
                LOG_ERROR("Message", "SSL_ERROR_SYSCALL: 시스템 호출 에러");
                break;
            case SSL_ERROR_SSL:
                LOG_ERROR("Message", "SSL_ERROR_SSL: SSL 프로토콜 에러");
                break;
            default:
                LOG_ERROR("Message", "알 수 없는 SSL 에러");
                break;
        }
        return NULL;
    }
    type = ntohl(type);

    // 인자 개수 수신
    uint32_t arg_count;
    ret = SSL_read(ssl, &arg_count, sizeof(arg_count));
    if (ret <= 0) {
        LOG_ERROR("Message", "인자 개수 수신 실패");
        return NULL;
    }
    arg_count = ntohl(arg_count);

    // 메시지 생성
    Message* message = create_message(type, NULL);
    if (!message) {
        LOG_ERROR("Message", "메시지 생성 실패");
        return NULL;
    }

    // 인자 수신
    for (uint32_t i = 0; i < arg_count; i++) {
        uint32_t arg_len;
        ret = SSL_read(ssl, &arg_len, sizeof(arg_len));
        if (ret <= 0) {
            LOG_ERROR("Message", "인자 길이 수신 실패");
            cleanup_message(message);
            return NULL;
        }
        arg_len = ntohl(arg_len);

        if (arg_len >= MAX_ARG_LENGTH) {
            LOG_ERROR("Message", "인자 길이가 너무 김");
            cleanup_message(message);
            return NULL;
        }

        ret = SSL_read(ssl, message->args[i], arg_len);
        if (ret <= 0) {
            LOG_ERROR("Message", "인자 내용 수신 실패");
            cleanup_message(message);
            return NULL;
        }
        message->args[i][arg_len] = '\0';
    }
    message->arg_count = arg_count;

    // 데이터 길이 수신
    uint32_t data_len;
    ret = SSL_read(ssl, &data_len, sizeof(data_len));
    if (ret <= 0) {
        LOG_ERROR("Message", "데이터 길이 수신 실패");
        cleanup_message(message);
        return NULL;
    }
    data_len = ntohl(data_len);

    // 데이터 수신
    if (data_len > 0) {
        if (data_len >= MAX_BUFFER_SIZE) {
            LOG_ERROR("Message", "데이터 길이가 너무 김");
            cleanup_message(message);
            return NULL;
        }

        ret = SSL_read(ssl, message->data, data_len);
        if (ret <= 0) {
            LOG_ERROR("Message", "데이터 내용 수신 실패");
            cleanup_message(message);
            return NULL;
        }
        message->data[data_len] = '\0';
    }

    LOG_INFO("Message", "메시지 수신 완료: 타입=%s", get_message_type_string(message->type));
    return message;
}

Message* create_pong_message(void) {
    LOG_INFO("Message", "Pong 메시지 생성");
    Message* message = create_message(MSG_PONG, NULL);
    if (!message) return NULL;
    message->arg_count = 0;
    return message;
}

/* DeviceStatus를 문자열로 변환하는 함수 */
const char* get_device_status_string(DeviceStatus status) {
    switch (status) {
        case DEVICE_AVAILABLE: return "available";
        case DEVICE_RESERVED: return "reserved";
        case DEVICE_MAINTENANCE: return "maintenance";
        default: return "unknown";
    }
}

