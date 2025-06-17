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



/* 예약 요청 메시지 생성 */
Message* create_reservation_message(const char* device_id) {
    Message* msg = create_message(MSG_RESERVE_REQUEST, NULL);
    if (!msg) return NULL;

    msg->args[msg->arg_count++] = strdup(device_id);
    return msg;
}

Message* create_error_message(const char* error_message) {
    Message* message = create_message(MSG_ERROR, error_message);
    if (!message) return NULL;
    return message;
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
        free(message); // [중요] 생성된 메시지 객체 자체도 해제
        return NULL;
    }
    arg_len = ntohl(arg_len);

    if (arg_len >= MAX_ARG_LENGTH) {
        LOG_ERROR("Message", "인자 길이가 너무 김: %u", arg_len);
        cleanup_message(message);
        free(message);
        return NULL;
    }

    // [수정] 인자를 저장할 메모리 할당
    message->args[i] = (char*)malloc(arg_len + 1);
    if (!message->args[i]) {
        LOG_ERROR("Message", "인자 메모리 할당 실패");
        cleanup_message(message);
        free(message);
        return NULL;
    }

    // [수정] 할당된 메모리에 인자 내용 수신
    ret = SSL_read(ssl, message->args[i], arg_len);
    if (ret <= 0) {
        LOG_ERROR("Message", "인자 내용 수신 실패");
        cleanup_message(message);
        free(message);
        return NULL;
    }
    message->args[i][arg_len] = '\0';
    message->arg_count++; // [중요] 성공적으로 읽은 인자 수 증가
}
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



/* DeviceStatus를 문자열로 변환하는 함수 */
const char* get_device_status_string(DeviceStatus status) {
    switch (status) {
        case DEVICE_AVAILABLE: return "available";
        case DEVICE_RESERVED: return "reserved";
        case DEVICE_MAINTENANCE: return "maintenance";
        default: return "unknown";
    }
}

