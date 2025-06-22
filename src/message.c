// message.c (최종 확인용)
#include "../include/message.h"
#include "../include/reservation.h"
#include "../include/network.h"

message_t* message_create(message_type_t type, const char *data) {
    message_t *msg = (message_t *)malloc(sizeof(message_t));
    if (!msg) {
        // LOG_ERROR("Message", "메시지 메모리 할당 실패");
        return NULL;
    }
    msg->type = type;
    msg->arg_count = 0;
    msg->error_code = ERROR_NONE; // 기본값 설정
    memset(msg->args, 0, sizeof(msg->args));
    if (data) {
        strncpy(msg->data, data, MAX_MESSAGE_LENGTH - 1);
        msg->data[MAX_MESSAGE_LENGTH - 1] = '\0';
    } else {
        msg->data[0] = '\0';
    }
    return msg;
}

bool message_fill_status_response_args(message_t* msg, const device_t* devices, int count, resource_manager_t* rm, reservation_manager_t* rvm) {
    if (!msg || !devices || !rm || !rvm) return false;
    msg->arg_count = 0;
    for (int i = 0; i < count; i++) {
        int base_idx = i * DEVICE_INFO_ARG_COUNT;
        if (base_idx + (DEVICE_INFO_ARG_COUNT - 1) >= MAX_ARGS) break;
        const char* status_str = message_get_device_status_string(devices[i].status);
        char end_time_str[32] = "0";
        char username_str[MAX_USERNAME_LENGTH] = "";
        if (devices[i].status == DEVICE_RESERVED) {
            reservation_t* res = reservation_get_active_for_device(rvm, rm, devices[i].id);
            if (res) {
                snprintf(end_time_str, sizeof(end_time_str), "%ld", res->end_time);
                strncpy(username_str, res->username, sizeof(username_str) - 1);
                username_str[sizeof(username_str) - 1] = '\0';
            }
        }
        msg->args[base_idx]     = strdup(devices[i].id);
        msg->args[base_idx + 1] = strdup(devices[i].name);
        msg->args[base_idx + 2] = strdup(devices[i].type);
        msg->args[base_idx + 3] = strdup(status_str);
        msg->args[base_idx + 4] = strdup(end_time_str);
        msg->args[base_idx + 5] = strdup(username_str);
        if (!msg->args[base_idx] || !msg->args[base_idx+1] || !msg->args[base_idx+2] || !msg->args[base_idx+3] || !msg->args[base_idx+4] || !msg->args[base_idx+5]) {
            return false;
        }
        msg->arg_count += DEVICE_INFO_ARG_COUNT;
    }
    return true;
}

message_t* message_create_status_response(const device_t *devices, int device_count, resource_manager_t* resource_manager, reservation_manager_t* reservation_manager) {
    message_t *message = message_create(MSG_STATUS_RESPONSE, NULL);
    if (!message) return NULL;
    if (!message_fill_status_response_args(message, devices, device_count, resource_manager, reservation_manager)) {
        message_destroy(message);
        return NULL;
    }
    return message;
}

message_t* message_create_error_with_code(error_code_t error_code, const char* error_message) {
    message_t* msg = message_create(MSG_ERROR, error_message);
    if (msg) {
        msg->error_code = error_code;
    }
    return msg;
}

const char *message_get_type_string(message_type_t type) {
    switch (type) {
        case MSG_LOGIN: return "LOGIN";
        case MSG_LOGOUT: return "LOGOUT";
        case MSG_RESERVE_REQUEST: return "RESERVE_REQUEST";
        case MSG_RESERVE_RESPONSE: return "RESERVE_RESPONSE";
        case MSG_CANCEL_REQUEST: return "CANCEL_REQUEST";
        case MSG_CANCEL_RESPONSE: return "CANCEL_RESPONSE";
        case MSG_STATUS_REQUEST: return "STATUS_REQUEST";
        case MSG_STATUS_RESPONSE: return "STATUS_RESPONSE";
        case MSG_STATUS_UPDATE: return "STATUS_UPDATE";
        case MSG_TIME_SYNC_REQUEST: return "TIME_SYNC_REQUEST";
        case MSG_TIME_SYNC_RESPONSE: return "TIME_SYNC_RESPONSE";
        case MSG_PING: return "PING";
        case MSG_PONG: return "PONG";
        case MSG_PING_RESPONSE: return "PING_RESPONSE";
        case MSG_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void message_destroy(message_t *msg) {
    if (!msg) return;
    for (int i = 0; i < msg->arg_count; i++) {
        if (msg->args[i]) {
            free(msg->args[i]);
            msg->args[i] = NULL;
        }
    }
    free(msg);
}

static bool message_read_arguments(SSL* ssl, message_t* msg, uint32_t arg_count) {
    for (uint32_t i = 0; i < arg_count; i++) {
        uint32_t arg_len_net;
        if (network_recv(ssl, &arg_len_net, sizeof(arg_len_net)) != sizeof(arg_len_net)) return false;
        uint32_t arg_len = ntohl(arg_len_net);
        if (arg_len >= MAX_ARG_LENGTH) return false;
        msg->args[i] = (char*)malloc(arg_len + 1);
        if (!msg->args[i]) return false;
        if (network_recv(ssl, msg->args[i], arg_len) != (ssize_t)arg_len) {
            free(msg->args[i]);
            msg->args[i] = NULL;
            return false;
        }
        msg->args[i][arg_len] = '\0';
        msg->arg_count++;
    }
    return true;
}

static bool message_read_data(SSL* ssl, message_t* msg) {
    uint32_t data_len_net;
    if (network_recv(ssl, &data_len_net, sizeof(data_len_net)) != sizeof(data_len_net)) return false;
    uint32_t data_len = ntohl(data_len_net);
    if (data_len > 0) {
        if (data_len >= MAX_MESSAGE_LENGTH) return false;
        if (network_recv(ssl, msg->data, data_len) != (ssize_t)data_len) return false;
        msg->data[data_len] = '\0';
    }
    return true;
}

static bool message_read_error_code(SSL* ssl, message_t* msg) {
    uint32_t error_code_net;
    if (network_recv(ssl, &error_code_net, sizeof(error_code_net)) != sizeof(error_code_net)) return false;
    msg->error_code = ntohl(error_code_net);
    return true;
}

message_t* message_receive(SSL* ssl) {
    uint32_t type_net, arg_count_net;
    if (network_recv(ssl, &type_net, sizeof(type_net)) != sizeof(type_net)) return NULL;
    if (network_recv(ssl, &arg_count_net, sizeof(arg_count_net)) != sizeof(arg_count_net)) return NULL;
    message_type_t type = ntohl(type_net);
    uint32_t arg_count = ntohl(arg_count_net);
    message_t* message = message_create(type, NULL);
    if (!message) return NULL;
    
    // 에러 코드 읽기 (MSG_ERROR 타입인 경우)
    if (type == MSG_ERROR) {
        if (!message_read_error_code(ssl, message)) {
            message_destroy(message);
            return NULL;
        }
    }
    
    if (!message_read_arguments(ssl, message, arg_count)) {
        message_destroy(message);
        return NULL;
    }
    if (!message_read_data(ssl, message)) {
        message_destroy(message);
        return NULL;
    }
    return message;
}

const char *message_get_device_status_string(device_status_t status) {
    switch (status) {
        case DEVICE_AVAILABLE: return "available";
        case DEVICE_RESERVED: return "reserved";
        case DEVICE_MAINTENANCE: return "maintenance";
        default: return "unknown";
    }
}

const char* message_get_error_string(error_code_t error_code) {
    switch (error_code) {
        case ERROR_NONE: return "성공";
        case ERROR_SESSION_AUTHENTICATION_FAILED: return "아이디 또는 비밀번호가 틀립니다";
        case ERROR_SESSION_ALREADY_EXISTS: return "이미 로그인된 사용자입니다";
        case ERROR_RESOURCE_IN_USE: return "장비를 사용할 수 없습니다";
        case ERROR_RESOURCE_MAINTENANCE_MODE: return "점검 중인 장비입니다";
        case ERROR_RESERVATION_ALREADY_EXISTS: return "이미 예약된 장비입니다";
        case ERROR_RESERVATION_NOT_FOUND: return "예약을 찾을 수 없습니다";
        case ERROR_RESERVATION_PERMISSION_DENIED: return "본인의 예약이 아닙니다";
        case ERROR_RESERVATION_INVALID_TIME: return "유효하지 않은 예약 시간입니다";
        case ERROR_UNKNOWN: return "서버 내부 오류가 발생했습니다";
        case ERROR_NETWORK_CONNECT_FAILED: return "네트워크 연결 오류가 발생했습니다";
        case ERROR_INVALID_PARAMETER: return "잘못된 요청입니다";
        case ERROR_SESSION_INVALID_STATE: return "세션이 만료되었습니다";
        case ERROR_PERMISSION_DENIED: return "권한이 없습니다";
        default: return "알 수 없는 오류가 발생했습니다";
    }
}